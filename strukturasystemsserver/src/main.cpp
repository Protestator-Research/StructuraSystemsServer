//
// Created by Moritz Herzog on 25.04.24.
//



#include "Controller/AuthenticationController.hpp"
#include "Controller/ProjectController.hpp"
#include "Controller/CommitController.hpp"
#include "Controller/DataBaseController.h"
#include "Controller/ElementsController.hpp"
#include "Controller/DigitalTwinController.hpp"
#include "Services/AuthenticationService.h"
#include "Filter/JwtFilter.hpp"
#include "Filter/AdministrationFilter.hpp"
#include "Filter/CorsFilter.hpp"

#include <drogon/drogon.h>

#define SCHEME "http"
#define HOST "127.0.0.1"
#define PORT 8088

int main() {
    drogon::app().loadConfigFile("config.json");
    const auto customConfig = drogon::app().getCustomConfig();

    std::string dbAddressArg = "localhost:27017";
    if (customConfig["mongo_address"].isString()) {
        std::string dbAddress = customConfig["mongo_address"].asString();
        if (!dbAddress.empty())
            dbAddressArg = dbAddress;
    }

    std::string dbUsernameArg = "";
    if (customConfig["mongo_username"].isString()) {
        std::string dbUsername = customConfig["mongo_username"].asString();
        if (!dbUsername.empty())
            dbUsernameArg = dbUsername;
    }

    std::string dbPasswordArg = "";
    if (customConfig["mongo_password"].isString()) {
        std::string dbPassword = customConfig["mongo_password"].asString();
        if (!dbPassword.empty())
            dbPasswordArg = dbPassword;
    }

    [[maybe_unused]] auto dbController = StructuraSystems::Server::DataBaseController::createInstance(dbAddressArg, dbUsernameArg, dbPasswordArg);
    [[maybe_unused]] auto jwtFilter = StructuraSystems::Server::JwtFilter();
    [[maybe_unused]] auto adminFilter = StructuraSystems::Server::AdministrationFilter();

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