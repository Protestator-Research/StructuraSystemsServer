//
// Created by Moritz Herzog on 25.04.24.
//



#include "Controller/AuthenticationController.hpp"
#include "Controller/GroupController.hpp"
#include "Controller/ProjectController.hpp"
#include "Controller/CommitController.hpp"
#include "Controller/DataBaseController.h"
#include "Controller/ElementsController.hpp"
#include "Controller/DigitalTwinController.hpp"
#include "Services/AuthenticationService.h"
#include "Services/LoginThrottle.h"
#include "Services/ServerConfig.h"
#include "Filter/JwtFilter.hpp"
#include "Filter/CorsFilter.hpp"

#include <drogon/drogon.h>
#include <sodium.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#define SCHEME "http"
#define HOST "127.0.0.1"
#define PORT 8088

namespace {
    std::string readEnv(const char* name) {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    }

    std::string readConfigString(const Json::Value& config, const char* key, std::string defaultValue) {
        if (config[key].isString()) {
            std::string value = config[key].asString();
            if (!value.empty())
                return value;
        }
        return defaultValue;
    }

    /// Reads a positive integer. Missing, non-numeric and non-positive values give the default.
    int64_t readConfigPositiveInt(const Json::Value& config, const char* key, int64_t defaultValue) {
        if (config[key].isIntegral() && config[key].asInt64() > 0)
            return config[key].asInt64();
        return defaultValue;
    }

    std::vector<std::string> readConfigStringArray(const Json::Value& config, const char* key, std::vector<std::string> defaultValue) {
        if (!config[key].isArray())
            return defaultValue;
        std::vector<std::string> result;
        for (const auto& entry : config[key]) {
            if (entry.isString())
                result.push_back(entry.asString());
        }
        return result;
    }
}

int main() {
    if (sodium_init() < 0) {
        std::cerr << "Failed to initialize libsodium" << std::endl;
        return 1;
    }

    std::string configPath = readEnv("STRUCTURA_CONFIG");
    if (configPath.empty())
        configPath = "config.json";

    drogon::app().loadConfigFile(configPath);
    const auto customConfig = drogon::app().getCustomConfig();

    std::string dbAddressArg = readConfigString(customConfig, "mongo_address", "localhost:27017");
    std::string dbUsernameArg = readConfigString(customConfig, "mongo_username", "");
    std::string dbPasswordArg = readConfigString(customConfig, "mongo_password", "");
    std::string dbDatabaseArg = readConfigString(customConfig, "mongo_database", "structura_systems");

    if (const auto value = readEnv("MONGO_ADDRESS"); !value.empty())
        dbAddressArg = value;
    if (const auto value = readEnv("MONGO_USERNAME"); !value.empty())
        dbUsernameArg = value;
    if (const auto value = readEnv("MONGO_PASSWORD"); !value.empty())
        dbPasswordArg = value;
    if (const auto value = readEnv("MONGO_DATABASE"); !value.empty())
        dbDatabaseArg = value;

    auto& serverConfig = StructuraSystems::Server::ServerConfig::get();
    serverConfig.TrustedProxies = readConfigStringArray(customConfig, "trusted_proxies", {"127.0.0.1"});
    serverConfig.SessionIdleTimeout = std::chrono::seconds(readConfigPositiveInt(customConfig, "session_idle_seconds", serverConfig.SessionIdleTimeout.count()));
    serverConfig.SessionMaxLifetime = std::chrono::seconds(readConfigPositiveInt(customConfig, "session_max_seconds", serverConfig.SessionMaxLifetime.count()));
    auto& loginLimits = serverConfig.LoginLimits;
    loginLimits.MaxFailuresPerUser = static_cast<std::size_t>(readConfigPositiveInt(customConfig, "login_max_failures_per_user", static_cast<int64_t>(loginLimits.MaxFailuresPerUser)));
    loginLimits.FailureWindow = std::chrono::seconds(readConfigPositiveInt(customConfig, "login_failure_window_seconds", loginLimits.FailureWindow.count()));
    loginLimits.MaxRequestsPerIp = static_cast<std::size_t>(readConfigPositiveInt(customConfig, "login_max_requests_per_ip", static_cast<int64_t>(loginLimits.MaxRequestsPerIp)));
    loginLimits.IpWindow = std::chrono::seconds(readConfigPositiveInt(customConfig, "login_ip_window_seconds", loginLimits.IpWindow.count()));
    [[maybe_unused]] const auto allowedOrigins = readConfigStringArray(customConfig, "allowed_origins", {});

    try {
        StructuraSystems::Server::DataBaseController::createInstance(dbAddressArg, dbUsernameArg, dbPasswordArg, dbDatabaseArg);
    } catch (const std::exception& e) {
        std::cerr << "Database startup failed: " << e.what() << std::endl;
        return 1;
    }
    [[maybe_unused]] auto jwtFilter = StructuraSystems::Server::JwtFilter();

    const auto& allowed_origin = customConfig["allowed_origin"].asString();

    std::function<void(const drogon::HttpRequestPtr&,const drogon::HttpResponsePtr&)> addHeader = [allowed_origin](const drogon::HttpRequestPtr& req,const drogon::HttpResponsePtr& resp)-> void {
        const auto origin = req->getHeader("Origin");

#ifndef NDEBUG
        if (!origin.empty()) {
            resp->addHeader("Access-Control-Allow-Origin", origin);
            resp->addHeader("Vary", "Origin");
        }
#else
        resp->addHeader("Access-Control-Allow-Origin", allowed_origin);
#endif
        resp->addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
        resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
        resp->addHeader("Access-Control-Max-Age", "86400");
        resp->addHeader("Access-Control-Allow-Credentials", "true");
    };

    // Housekeeping for sessions and login throttling. The loop is only available once the framework runs.
    drogon::app().registerBeginningAdvice([] {
        drogon::app().getLoop()->runEvery(300.0, [] {
            StructuraSystems::Server::AuthenticationService::getInstance()->purgeExpiredSessions();
            StructuraSystems::Server::LoginThrottle::getInstance().purgeExpired();
        });
    });

    drogon::app()
        .registerPreRoutingAdvice([addHeader](const drogon::HttpRequestPtr& req, drogon::AdviceCallback&& acb, drogon::AdviceChainCallback&& accb)
          {
            if (req->method() == drogon::Options) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                addHeader(req, resp);
                acb(resp);
                return;
            }
            accb();
          })
        .registerPostHandlingAdvice(addHeader)
        .setThreadNum(std::thread::hardware_concurrency() / 2)
        .run();
}