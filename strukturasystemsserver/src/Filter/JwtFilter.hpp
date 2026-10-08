//
// Created by Moritz Herzog on 24.02.26.
//

#pragma once

#include <drogon/HttpFilter.h>

#include "../Services/AuthenticationService.h"
#include "../Controller/Helper/RequestHelper.hpp"

namespace StructuraSystems::Server {
    class JwtFilter : public drogon::HttpFilter<StructuraSystems::Server::JwtFilter> {
    public:

        void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&fcb, drogon::FilterChainCallback &&fccb) override {
            auto &authHeader = req->getHeader("Authorization");

            if (authHeader.empty() || authHeader.compare(0, 7, "Bearer ") != 0) {
                auto res = drogon::HttpResponse::newHttpResponse();
                res->setStatusCode(drogon::k401Unauthorized);
                fcb(res);
                return;
            }

            std::string token = authHeader.substr(7);

            try {

                if (AuthenticationService::getInstance()->isTokenAuthenticated(token)) {
                    const auto user = AuthenticationService::getInstance()->getUserFromDatabase(token);
                    if (!user) {
                        auto res = drogon::HttpResponse::newHttpResponse();
                        res->setStatusCode(drogon::k401Unauthorized);
                        fcb(res);
                        return;
                    }

                    if (user->mustChangePassword() && !isAllowedWithPendingPasswordChange(req)) {
                        fcb(jsonError(drogon::k403Forbidden, "password_change_required"));
                        return;
                    }

                    fccb();
                    return;
                }

                auto res = drogon::HttpResponse::newHttpResponse();
                res->setStatusCode(drogon::k401Unauthorized);
                fcb(res);

            } catch (...) {
                auto res = drogon::HttpResponse::newHttpResponse();
                res->setStatusCode(drogon::k401Unauthorized);
                fcb(res);
            }
        }

    private:
        static bool isAllowedWithPendingPasswordChange(const drogon::HttpRequestPtr &req) {
            return (req->path() == "/me" && req->method() == drogon::Get) ||
                   (req->path() == "/me/password" && req->method() == drogon::Put);
        }
    };
}
