//
// Created by Moritz Herzog on 25.02.26.
//

#pragma once

#include <drogon/HttpFilter.h>

#include "AuthenticationService.h"

namespace StructuraSystems::Server {
    class AdministrationFilter : public drogon::HttpFilter<StructuraSystems::Server::AdministrationFilter> {
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

                if (AuthenticationService::getInstance()->isUserAuthenticationAdiministrator(token)) {
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
    };
}
