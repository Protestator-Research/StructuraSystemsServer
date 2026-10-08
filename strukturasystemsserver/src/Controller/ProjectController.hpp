#pragma once

#include <sysmlv2/rest/entities/Project.h>
#include <boost/uuid/string_generator.hpp>

#include <drogon/HttpController.h>

#include "../Services/ServerProjectService.h"
#include "../Entities/db/Project.hpp"
#include "../Filter/JwtFilter.hpp"
#include "Helper/RequestHelper.hpp"
#include "Helper/ProjectAccessHelper.hpp"
#include "DataBaseController.h"

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
		ADD_METHOD_TO(ProjectController::changeOwner,"/projects/{1:projectId}/owner",drogon::Put, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END

		void getProjects(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto user = currentUser(req);
			if (!user)
			{
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}

			const auto returnValue = _ProjectService->serializeProjects([&user](const std::shared_ptr<Project>& project) {
				return canAccessProject(*user, project, ProjectAccess::Read);
			});

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(returnValue);
			callback(response);
		}

		void getProjectWithId(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) const {
			const auto project = withProject(req, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(_ProjectService->serializeProject(project));
			callback(response);
		}

		void deleteProjectWithId(const drogon::HttpRequestPtr &req, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) const {
			const auto existingProject = withProject(req, projectId, ProjectAccess::Delete, callback);
			if (!existingProject)
				return;

			const auto project = _ProjectService->deleteProject(existingProject->getId());
			if (project==nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Project not found."));
				return;
			}

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(project->serializeToJson());
			callback(response);
		}

		void createProject(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto user = currentUser(request);
			if (!user)
			{
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}

			const auto jsonObject = requireJson(request);
			if (jsonObject == nullptr || !jsonObject->isObject())
			{
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}
			const auto& requestObject = *jsonObject;

			// The owner is always the authenticated user; a client supplied owner is ignored.
			const auto owner = user->username();
			std::string group = request->getParameter("owningGroup");

			std::string name;
			std::string description;
			try {
				name = requestObject["name"].asString();
				description = requestObject["description"].asString();
				if (requestObject.isMember("owningGroup"))
				{
					if (!requestObject["owningGroup"].isString())
					{
						callback(jsonError(drogon::k400BadRequest, "owningGroup must be a string."));
						return;
					}
					group = requestObject["owningGroup"].asString();
				}
			}
			catch (const Json::Exception&) {
				callback(jsonError(drogon::k400BadRequest, "Invalid project description."));
				return;
			}

			if (!mayUseGroup(*user, group))
			{
				callback(jsonError(drogon::k403Forbidden, "You are not a member of the owning group."));
				return;
			}

			const auto newProject = _ProjectService->createProject(name, description, owner, group);
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created, drogon::CT_APPLICATION_JSON);
			response->setBody(newProject->serializeToJson());
			callback(response);
		}

		void changeOwner(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) const {
			User user;
			const auto project = withProject(request, projectId, ProjectAccess::Delete, callback, &user);
			if (!project)
				return;

			const auto jsonObject = requireJson(request);
			if (jsonObject == nullptr || !jsonObject->isObject())
			{
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}
			const auto& requestObject = *jsonObject;

			const bool hasOwner = requestObject.isMember("owner");
			const bool hasGroup = requestObject.isMember("owningGroup");
			if ((!hasOwner && !hasGroup) ||
				(hasOwner && !requestObject["owner"].isString()) ||
				(hasGroup && !requestObject["owningGroup"].isString()))
			{
				callback(jsonError(drogon::k400BadRequest, "Expected string fields owner and/or owningGroup."));
				return;
			}

			std::optional<std::string> newOwner;
			std::optional<std::string> newGroup;
			if (hasOwner)
			{
				newOwner = requestObject["owner"].asString();
				if (!AuthenticationService::getInstance()->findUser(*newOwner))
				{
					callback(jsonError(drogon::k400BadRequest, "The new owner does not exist."));
					return;
				}
			}
			if (hasGroup)
			{
				newGroup = requestObject["owningGroup"].asString();
				if (!mayUseGroup(user, *newGroup))
				{
					callback(jsonError(drogon::k403Forbidden, "You are not a member of the owning group."));
					return;
				}
			}

			const auto changed = _ProjectService->changeOwnership(project->getId(), newOwner, newGroup);
			if (changed == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Project not found."));
				return;
			}

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(_ProjectService->serializeProject(changed));
			callback(response);
		}

	private:
		std::shared_ptr<ServerProjectService> _ProjectService = ServerProjectService::getInstance();

		/// An owning group may only be set by its members and by administrators.
		static bool mayUseGroup(const User& user, const std::string& group)
		{
			return group.empty() || user.role() == USER_ROLE::GLOBAL_ADMINISTRATOR || user.group() == group;
		}
	};
}
