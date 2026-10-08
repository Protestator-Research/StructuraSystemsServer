#pragma once

#include <sysmlv2/rest/entities/Project.h>
#include <boost/uuid/string_generator.hpp>

#include <drogon/HttpController.h>

#include "../Services/ServerProjectService.h"
#include "../Entities/db/Project.hpp"
#include "../Filter/JwtFilter.hpp"
#include "Helper/RequestHelper.hpp"

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

		void getProjects(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto authHeader = req->getHeader("Authorization");
			const auto& user = AuthenticationService::getInstance()->getUserFromDatabase(authHeader.substr(7));

			const auto& projects = _ProjectService->getProjects();

			std::vector<std::shared_ptr<SysMLv2::REST::Project>> userAllowedProjects;
			for (const auto& project : projects)
			{
				const auto db_project = std::dynamic_pointer_cast<StructuraSystems::Server::Project>(project);

				if (isUsersAccessAllowed(user, db_project))
					userAllowedProjects.push_back(project);
			}

			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < userAllowedProjects.size(); i++)
			{
				returnValue += userAllowedProjects[i]->serializeToJson();
				if (i < (userAllowedProjects.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(returnValue);
			callback(response);
		}

		void getProjectWithId(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) const {
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);

			const auto authHeader = req->getHeader("Authorization");
			const auto& user = AuthenticationService::getInstance()->getUserFromDatabase(authHeader.substr(7));

			const auto parsedProjectId = parseUuid(projectId);
			if (!parsedProjectId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid project id."));
				return;
			}

			const auto project = _ProjectService->getProjectById(*parsedProjectId);
			if (project==nullptr)
            {
                const auto notFoundResponse = drogon::HttpResponse::newHttpResponse(drogon::k404NotFound, drogon::CT_APPLICATION_JSON);
                callback(notFoundResponse);
                return;
            }
			if (!isUsersAccessAllowed(user,std::dynamic_pointer_cast<Project>(project)))
			{
				const auto error_response = drogon::HttpResponse::newHttpResponse(drogon::k401Unauthorized, drogon::CT_APPLICATION_JSON);
				callback(error_response);
				return;
			}

			response->setBody(project->serializeToJson());
			callback(response);
		}

		void deleteProjectWithId(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) const {
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);

			const auto authHeader = req->getHeader("Authorization");
			const auto& user = AuthenticationService::getInstance()->getUserFromDatabase(authHeader.substr(7));

			const auto parsedProjectId = parseUuid(projectId);
			if (!parsedProjectId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid project id."));
				return;
			}

			const auto existingProject = _ProjectService->getProjectById(*parsedProjectId);
			if (existingProject==nullptr)
            {
                const auto error_response = drogon::HttpResponse::newHttpResponse(drogon::k404NotFound, drogon::CT_APPLICATION_JSON);
                callback(error_response);
                return;
            }
			if (!isUsersAccessAllowed(user,std::dynamic_pointer_cast<Project>(existingProject)))
			{
				const auto error_response = drogon::HttpResponse::newHttpResponse(drogon::k401Unauthorized, drogon::CT_APPLICATION_JSON);
				callback(error_response);
				return;
			}

			const auto project = _ProjectService->deleteProject(*parsedProjectId);
			if (project==nullptr)
            {
                const auto error_response = drogon::HttpResponse::newHttpResponse(drogon::k404NotFound, drogon::CT_APPLICATION_JSON);
                callback(error_response);
                return;
            }

			response->setBody(project->serializeToJson());
			callback(response);
		}

		void createProject(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto jsonObject = requireJson(request);
			if (jsonObject == nullptr || !jsonObject->isObject())
			{
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}
			const auto& requestObject = *jsonObject;

			const auto owner = request->getParameter("owner");
			const auto group = request->getParameter("owningGroup");

			std::string name;
			std::string description;
			try {
				name = requestObject["name"].asString();
				description = requestObject["description"].asString();
			}
			catch (const Json::Exception&) {
				callback(jsonError(drogon::k400BadRequest, "Invalid project description."));
				return;
			}

			const auto newProject = _ProjectService->createProject(name, description, owner, group);
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created, drogon::CT_APPLICATION_JSON);
			response->setBody(newProject->serializeToJson());
			callback(response);
		}

	private:
		std::shared_ptr<ServerProjectService> _ProjectService = ServerProjectService::getInstance();

		bool isUsersAccessAllowed(const User& user, std::shared_ptr<Project> project) const
		{
			return (((project->getOwner().empty()) && project->getOwningGroup().empty()) ||
				(user.username() == project->getOwner()) ||
				(user.group()==project->getOwningGroup()));
		}

	};
}
