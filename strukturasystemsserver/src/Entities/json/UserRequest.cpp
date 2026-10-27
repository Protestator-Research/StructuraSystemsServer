//
// Created by Moritz Herzog on 26.02.26.
//

#include "UserRequest.h"

#include <regex>
#include <nlohmann/json.hpp>

#include "../db/User.hpp"
#include "../../Services/AccessControlService.h"

namespace StructuraSystems {
    namespace Server {
        namespace {
            constexpr size_t MinPasswordLength = 12;
        }

        bool UserRequest::isValidUsername(const std::string& username) {
            static const std::regex pattern("^[A-Za-z0-9._-]{3,64}$");
            return std::regex_match(username, pattern);
        }

        bool UserRequest::isValidGroup(const std::string& group) {
            static const std::regex pattern("^[A-Za-z0-9._-]{1,64}$");
            return group.empty() || std::regex_match(group, pattern);
        }

        bool UserRequest::isValidPassword(const std::string& password) {
            return password.size() >= MinPasswordLength;
        }

        UserRequest::UserRequest(std::string jsonString, bool forCreate) {
            nlohmann::json json;
            try {
                json = nlohmann::json::parse(jsonString);
            } catch (const nlohmann::json::exception&) {
                throw UserRequestValidationException("Invalid JSON.");
            }
            if (!json.is_object())
                throw UserRequestValidationException("Request body must be a JSON object.");

            if (forCreate) {
                if (!json.contains("username") || !json["username"].is_string())
                    throw UserRequestValidationException("username is required.");
                Username = json["username"].get<std::string>();
                if (!isValidUsername(Username))
                    throw UserRequestValidationException("username must match ^[A-Za-z0-9._-]{3,64}$.");
            }

            if (json.contains("password")) {
                if (!json["password"].is_string())
                    throw UserRequestValidationException("password must be a string.");
                Password = json["password"].get<std::string>();
                if (!isValidPassword(Password))
                    throw UserRequestValidationException("password must have at least 12 characters.");
                HasPassword = true;
            } else if (forCreate) {
                throw UserRequestValidationException("password is required.");
            }

            if (json.contains("group")) {
                if (!json["group"].is_string())
                    throw UserRequestValidationException("group must be a string.");
                Group = json["group"].get<std::string>();
                if (!isValidGroup(Group))
                    throw UserRequestValidationException("group must be empty or match ^[A-Za-z0-9._-]{1,64}$.");
                HasGroup = true;
            }

            if (json.contains("role")) {
                if (!json["role"].is_number_integer())
                    throw UserRequestValidationException("role must be an integer.");
                const auto role = json["role"].get<long long>();
                if (role < 0 || role > 1000 || !isValidRole(static_cast<int>(role)))
                    throw UserRequestValidationException("role must be one of 0, 1, 2, 100.");
                Role = static_cast<int>(role);
                HasRole = true;
            }
        }

        std::string UserRequest::getUsername() {
            return Username;
        }

        bool UserRequest::isPasswordChanged() {
            return HasPassword;
        }

        std::string UserRequest::getPassword() {
            return Password;
        }

        bool UserRequest::isGroupChanged() {
            return HasGroup;
        }

        std::string UserRequest::getGroup() {
            return Group;
        }

        bool UserRequest::isUserRoleChanged() {
            return HasRole;
        }

        int UserRequest::getUserRole() {
            return Role;
        }
    } // Server
} // StructuraSystems
