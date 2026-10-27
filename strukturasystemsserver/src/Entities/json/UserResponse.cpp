//
// Created by Moritz Herzog on 26.02.26.
//

#include "UserResponse.h"

namespace StructuraSystems::Server {
    UserResponse::UserResponse(const User &user) {
        Username = user.username();
        Group = user.group();
        UserRole = user.role();
        MustChangePassword = user.mustChangePassword();

    }

    std::string UserResponse::serializeToJson() const{
        nlohmann::json json;
        json["username"] = Username;
        json["group"] = Group;
        json["role"] = UserRole;
        json["mustChangePassword"] = MustChangePassword;
        return json.dump();
    }

} // StructuraSystems::Server