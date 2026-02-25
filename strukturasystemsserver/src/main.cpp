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
#include "Services/JwtFilter.hpp"
#include "Services/AdministrationFilter.hpp"

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

    drogon::app()
        .setThreadNum(std::thread::hardware_concurrency() / 2)
        .run();
}