//
// Created by Moritz Herzog on 26.02.26.
//

#pragma once

#include <drogon/HttpFilter.h>

namespace StructuraSystems::Server {
    class CorsFilter : public drogon::HttpFilter<StructuraSystems::Server::CorsFilter>
    {
    public:
        void doFilter(const drogon::HttpRequestPtr& req,
                  drogon::FilterCallback&& cb,
                  drogon::FilterChainCallback&& fccb) override {

            if (req->method() == drogon::Options) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                addCorsHeaders(resp);
                resp->setStatusCode(drogon::k204NoContent);
                cb(resp);
                return;
            }

            // Header für spätere Response vormerken
            req->attributes()->insert("cors", true);

            fccb();
        }

    private:
        void addCorsHeaders(const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
            resp->addHeader("Access-Control-Max-Age", "86400");
        }
    };
}