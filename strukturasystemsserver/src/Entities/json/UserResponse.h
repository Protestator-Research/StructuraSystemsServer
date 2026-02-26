//
// Created by Moritz Herzog on 26.02.26.
//

#pragma once

#include "../db/User.hpp"

namespace StructuraSystems::Server {
    class UserResponse {
    public:
        UserResponse(const User& user);
        ~UserResponse() = default;

        std::string serializeToJson() const;

    private:
        std::string Username;
        std::string Group;
        int UserRole;
    };
} // StructuraSystems::Server

