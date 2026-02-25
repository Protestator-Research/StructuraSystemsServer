#pragma once

#include <sysmlv2/rest/entities/Project.h>
#include <boost/uuid/string_generator.hpp>

#include <drogon/HttpController.h>

#include "../Services/ServerProjectService.h"
#include "../Services/JwtFilter.hpp"

namespace StructuraSystems::Server
{
	class ProjectController : public drogon::HttpController<ProjectController>
	{
	public:
		~ProjectController() override = default;

		METHOD_LIST_BEGIN
		ADD_METHOD_TO(ProjectController::getProjects,"/projects",drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ProjectController::getProjectWithId,"/projects/{1:projectId}",drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ProjectController::deleteProjectWithId,"/projects/{1:projectId}",drogon::Delete, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ProjectController::createProject,"/projects",drogon::Post, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END

		void getProjects(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
		{
			const auto& projects = _ProjectService->getProjects();
			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < projects.size(); i++)
			{
				returnValue += projects[i]->serializeToJson();
				if (i < (projects.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(returnValue);
			callback(response);
		}

		void getProjectWithId(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(_ProjectService->getProjectById(boost::uuids::string_generator()(projectId))->serializeToJson());
			callback(response);
		}

		void deleteProjectWithId(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(_ProjectService->deleteProject(boost::uuids::string_generator()(projectId))->serializeToJson());
			callback(response);
		}

		void createProject(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
		{
			auto requestObject = *request->getJsonObject();
			const auto newProject = _ProjectService->createProject(requestObject["name"].asString(),requestObject["description"].asString());
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created, drogon::CT_APPLICATION_JSON);
			response->setBody(newProject->serializeToJson());
			callback(response);
		}

		std::shared_ptr<ServerProjectService> _ProjectService = ServerProjectService::getInstance();

	};
}
