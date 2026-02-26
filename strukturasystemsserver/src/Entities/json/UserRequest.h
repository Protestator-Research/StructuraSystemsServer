//
// Created by Moritz Herzog on 26.02.26.
//

#pragma once

#include <string>

namespace StructuraSystems::Server {
    class UserRequest {
    public:
        UserRequest() = default;
        UserRequest(std::string jsonString);
        ~UserRequest() = default;

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
        int Role = -1;
    };
} // StructuraSystems::Server

