#pragma once

#include <sysmlv2/rest/entities/Project.h>
#include <boost/uuid/string_generator.hpp>

#include <drogon/HttpController.h>

#include "../Services/ServerProjectService.h"
#include "../Entities/db/Project.hpp"
#include "../Filter/JwtFilter.hpp"

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

			const auto project = _ProjectService->getProjectById(boost::uuids::string_generator()(projectId));
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

			const auto project = _ProjectService->deleteProject(boost::uuids::string_generator()(projectId));
			if (project==nullptr)
            {
                const auto error_response = drogon::HttpResponse::newHttpResponse(drogon::k404NotFound, drogon::CT_APPLICATION_JSON);
                callback(error_response);
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

		void createProject(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			auto requestObject = *request->getJsonObject();

			const auto owner = request->getParameter("owner");
			const auto group = request->getParameter("owningGroup");

			const auto newProject = _ProjectService->createProject(requestObject["name"].asString(),requestObject["description"].asString(), owner,group);
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
