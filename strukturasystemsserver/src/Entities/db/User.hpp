//
// Created by Moritz Herzog on 25.02.26.
//

#pragma once

#include <string>
#include <utility>
#include <nlohmann/json.hpp>

namespace StructuraSystems::Server {
    enum USER_ROLE {
        USER = 0,
        GROUP_MANAGER = 1,
        GLOBAL_MODERATOR = 2,
        GLOBAL_ADMINISTRATOR = 100
    };

    class User {
    public:
        User() = default;

        User(std::string username, std::string hashedPassword, std::string group, USER_ROLE role = USER_ROLE::USER) {
            Username = username;
            HashedPassword = hashedPassword;
            Group = group;
            Role = role;
        }

        User(std::string jsonString) {
            nlohmann::json json = nlohmann::json::parse(jsonString);
            Username = json["_id"];
            HashedPassword = json["securityString"];
            if (json.contains("group") && json["group"].is_string())
                Group = json["group"].get<std::string>();
            Role = USER_ROLE::USER;
            if (json.contains("role") && json["role"].is_number_integer())
                Role = static_cast<USER_ROLE>(json["role"].get<int>());
            if (json.contains("mustChangePassword") && json["mustChangePassword"].is_boolean())
                MustChangePassword = json["mustChangePassword"].get<bool>();
        }

        User(const User& other) {
            Username = other.Username;
            HashedPassword = other.HashedPassword;
            Group = other.Group;
            Role = other.Role;
            MustChangePassword = other.MustChangePassword;
        }

        ~User() = default;

        std::string username() const{
            return Username;
        }

        std::string hashedPassword() const{
            return HashedPassword;
        }

        void setNewHashedPassword(std::string newHashedPassword) {
            HashedPassword = newHashedPassword;
        }

        std::string group() const{
            return Group;
        }

        void setGroup(std::string group) {
            Group = std::move(group);
        }

        bool mustChangePassword() const{
            return MustChangePassword;
        }

        void setMustChangePassword(bool mustChangePassword) {
            MustChangePassword = mustChangePassword;
        }

        USER_ROLE role() const{
            return Role;
        }

        void setRole(USER_ROLE role) {
            Role = role;
        }

        std::string getJson() const{
            nlohmann::json json;
            json["_id"] = Username;
            json["securityString"] = HashedPassword;
            json["group"] = Group;
            json["role"] = static_cast<int>(Role);
            json["mustChangePassword"] = MustChangePassword;
            return json.dump();
        }

        User& operator=(const User& other) {
            Username = other.Username;
            HashedPassword = other.HashedPassword;
            Group = other.Group;
            Role = other.Role;
            MustChangePassword = other.MustChangePassword;
            return *this;
        }

    private:
        std::string Username;
        std::string HashedPassword;
        std::string Group = "";
        USER_ROLE Role = USER_ROLE::USER;
        bool MustChangePassword = false;
    };
}
