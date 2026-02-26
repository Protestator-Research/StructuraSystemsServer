//
// Created by Moritz Herzog on 26.02.26.
//

#include "UserRequest.h"

#include <regex>
#include <nlohmann/json.hpp>

namespace StructuraSystems {
    namespace Server {
        UserRequest::UserRequest(std::string jsonString) {
            nlohmann::json json = nlohmann::json::parse(jsonString);
            Username = json["username"].get<std::string>();

            if (json.contains("password"))
                Password = json["password"].get<std::string>();

            if (json.contains("group"))
                Group = json["group"].get<std::string>();

            if (json.contains("role"))
                Role = json["role"].get<int>();
        }

        std::string UserRequest::getUsername() {
            return Username;
        }

        bool UserRequest::isPasswordChanged() {
            return (Password != "");
        }

        std::string UserRequest::getPassword() {
            return Password;
        }

        bool UserRequest::isGroupChanged() {
            return (Group != "");
        }

        std::string UserRequest::getGroup() {
            return Group;
        }

        bool UserRequest::isUserRoleChanged() {
            return (Role != -1);
        }

        int UserRequest::getUserRole() {
            return Role;
        }
    } // Server
} // StructuraSystems