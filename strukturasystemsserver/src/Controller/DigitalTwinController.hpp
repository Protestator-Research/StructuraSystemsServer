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
#include "../Filter/JwtFilter.hpp"
#include "Helper/RequestHelper.hpp"

namespace StructuraSystems::Server {
    class DigitalTwinController : public drogon::HttpController<StructuraSystems::Server::DigitalTwinController> {
    public:
        METHOD_LIST_BEGIN
        ADD_METHOD_TO(DigitalTwinController::getAllTwins, "/projects/{1:projectId}/twins", drogon::Get, "StructuraSystems::Server::JwtFilter");
        ADD_METHOD_TO(DigitalTwinController::postTwin, "/projects/{1:projectId}/twins", drogon::Post, "StructuraSystems::Server::JwtFilter");
        ADD_METHOD_TO(DigitalTwinController::getTwinData, "/project/{1:projectId}/twins/{2:tagId}/data", drogon::Get, "StructuraSystems::Server::JwtFilter");
        METHOD_LIST_END

        void getAllTwins(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) {
            const auto parsedProjectId = parseUuid(projectId);
            if (!parsedProjectId)
            {
                callback(jsonError(drogon::k400BadRequest, "Invalid project id."));
                return;
            }

            const auto project = ProjectNavigationService->getProjectById(*parsedProjectId);

            if (project==nullptr)
            {
                const auto response = drogon::HttpResponse::newHttpResponse(drogon::k404NotFound, drogon::CT_APPLICATION_JSON);
                callback(response);
                return;
            }

            const auto twins = DTService->getAllTwinsForProject(project);

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
            const auto jsonObject = requireJson(request);
            if (jsonObject == nullptr)
            {
                callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
                return;
            }

            std::shared_ptr<TwinRequest> twinRequest;
            std::string twinName;
            std::shared_ptr<SysMLv2::REST::Commit> referencedCommit;
            try {
                twinRequest = std::make_shared<TwinRequest>(jsonObject->toStyledString());
                twinName = twinRequest->name();
                referencedCommit = twinRequest->referencedCommit();
            } catch (...) {
                twinRequest = nullptr;
            }
            if (twinRequest == nullptr || twinName.empty() || referencedCommit == nullptr)
            {
                callback(jsonError(drogon::k400BadRequest, "Invalid twin request."));
                return;
            }

            const auto parsedProjectId = parseUuid(projectId);
            if (!parsedProjectId)
            {
                callback(jsonError(drogon::k400BadRequest, "Invalid project id."));
                return;
            }

            const auto project = ProjectNavigationService->getProjectById(*parsedProjectId);
            if (project == nullptr)
            {
                callback(drogon::HttpResponse::newHttpResponse(drogon::k404NotFound, drogon::CT_APPLICATION_JSON));
                return;
            }

            const auto commit = ProjectVerService->getCommitById(project,referencedCommit->getId());
            if (commit == nullptr)
            {
                callback(drogon::HttpResponse::newHttpResponse(drogon::k404NotFound, drogon::CT_APPLICATION_JSON));
                return;
            }

            const auto newDigitalTwin = DTService->createTwin(twinName, project,commit);

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
        std::shared_ptr<DigitalTwinService> DTService = DigitalTwinService::getInstance();

    };
}
