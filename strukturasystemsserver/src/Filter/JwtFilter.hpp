//
// Created by Moritz Herzog on 24.02.26.
//

#pragma once

#include <drogon/HttpFilter.h>

#include "../Services/AccessControlService.h"
#include "../Services/AuthContext.h"
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

                // One lookup per request: a login session (idle and maximum lifetime checked, nothing for a user that no
                // longer exists) or an API token (nothing for unknown, expired and revoked ones).
                const auto context = AuthenticationService::getInstance()->authenticate(token);
                if (!context) {
                    auto res = drogon::HttpResponse::newHttpResponse();
                    res->setStatusCode(drogon::k401Unauthorized);
                    fcb(res);
                    return;
                }

                if (context->isSession()) {
                    if (context->user()->mustChangePassword() && !isAllowedWithPendingPasswordChange(req)) {
                        fcb(jsonError(drogon::k403Forbidden, "password_change_required"));
                        return;
                    }
                }
                else {
                    switch (isAllowedForApiToken(req->methodString(), req->path(), context->apiToken()->scope())) {
                        case ApiTokenPathDecision::Allowed:
                            break;
                        case ApiTokenPathDecision::SessionRequired:
                            fcb(jsonError(drogon::k403Forbidden, "session_required"));
                            return;
                        case ApiTokenPathDecision::InsufficientScope:
                            fcb(jsonError(drogon::k403Forbidden, "insufficient_scope"));
                            return;
                    }
                }

                req->attributes()->insert(AUTH_CONTEXT_ATTRIBUTE, std::make_shared<const AuthContext>(*context));

                fccb();

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
