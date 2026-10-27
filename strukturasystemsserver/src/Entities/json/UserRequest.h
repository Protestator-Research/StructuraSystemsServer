//
// Created by Moritz Herzog on 26.02.26.
//

#pragma once

#include <stdexcept>
#include <string>

namespace StructuraSystems::Server {
    /// Thrown when a user request does not pass validation. Controllers map it to HTTP 400.
    class UserRequestValidationException : public std::runtime_error {
    public:
        explicit UserRequestValidationException(const std::string& message) : std::runtime_error(message) {}
    };

    class UserRequest {
    public:
        UserRequest() = default;
        /// forCreate: username and password are required (POST). Otherwise (PUT) the
        /// username is taken from the path and every field is optional.
        explicit UserRequest(std::string jsonString, bool forCreate = true);
        ~UserRequest() = default;

        static bool isValidUsername(const std::string& username);
        static bool isValidGroup(const std::string& group);
        static bool isValidPassword(const std::string& password);

        std::string getUsername();
        bool isPasswordChanged();
        std::string getPassword();
        bool isGroupChanged();
        std::string getGroup();
        bool isUserRoleChanged();
        int getUserRole();

    private:
        std::string Username = "";
        std::string Password = "";
        std::string Group = "";
        int Role = 0;
        bool HasPassword = false;
        bool HasGroup = false;
        bool HasRole = false;
    };
} // StructuraSystems::Server
