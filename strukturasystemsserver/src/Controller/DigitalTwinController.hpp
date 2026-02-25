//
// Created by Moritz Herzog on 24.02.26.
//

#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <memory>

#include "../Entities/json/TwinRequest.h"

#include "../Services/ProjectVersioningService.h"
#include "../Services/ServerProjectService.h"
#include "../Services/DigitalTwinService.h"
#include "../Services/JwtFilter.hpp"

namespace StructuraSystems::Server {
    class DigitalTwinController : public drogon::HttpController<StructuraSystems::Server::DigitalTwinController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(DigitalTwinController::getAllTwins,"/projects/{1:projectId}/twins", drogon::Get, "StructuraSystems::Server::JwtFilter");
        ADD_METHOD_TO(DigitalTwinController::postTwin, "/projects/{1:projectId}/twins", drogon::Post, "StructuraSystems::Server::JwtFilter");
        ADD_METHOD_TO(DigitalTwinController::getTwinData, "/project/{1:projectId}/twins/{2:tagId}/data", drogon::Get, "StructuraSystems::Server::JwtFilter");
        METHOD_LIST_END

        void getAllTwins(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) {
            const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
            const auto twins = DigitalTwinService->getAllTwinsForProject(project);

            std::string returnValue = "[\r\n";
            for (size_t i = 0; i < twins.size(); i++)
            {
                returnValue += twins[i]->serializeToJson();
                if (i < (twins.size() - 1))
                    returnValue += ",\r\n";
            }
            returnValue += "]";

            const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
            response->setBody(returnValue);
            callback(response);
        }

        void postTwin(const drogon::HttpRequestPtr & request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) {
            const std::string json = request->getJsonObject()->toStyledString();
            const std::shared_ptr<TwinRequest> twinRequest = std::make_shared<TwinRequest>(json);
            const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
            const auto commit = ProjectVerService->getCommitById(project,twinRequest->referencedCommit()->getId());
            const auto newDigitalTwin = DigitalTwinService->createTwin(twinRequest->name(), project,commit);

            const auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created, drogon::CT_APPLICATION_JSON);
            response->setBody(newDigitalTwin->serializeToJson());
            callback(response);
        }

        void getTwinData([[maybe_unused]] const drogon::HttpRequestPtr & request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, [[maybe_unused]] const std::string& projectId, [[maybe_unused]] const std::string& tagId) {
            const auto response = drogon::HttpResponse::newHttpResponse(drogon::k501NotImplemented, drogon::CT_APPLICATION_JSON);
            callback(response);
        }

    private:
        std::shared_ptr<ProjectVersioningService> ProjectVerService = ProjectVersioningService::getInstance();
        std::shared_ptr<ServerProjectService> ProjectNavigationService = ServerProjectService::getInstance();
        std::shared_ptr<DigitalTwinService> DigitalTwinService = DigitalTwinService::getInstance();

    };
}
