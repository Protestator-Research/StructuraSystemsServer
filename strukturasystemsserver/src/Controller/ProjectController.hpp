#pragma once

#include <sysmlv2/rest/entities/Project.h>
#include <boost/uuid/string_generator.hpp>

#include <drogon/HttpController.h>

#include "../Services/ServerProjectService.h"
#include "../Services/ProjectVersioningService.h"
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
		ADD_METHOD_TO(ProjectController::updateProject,"/projects/{1:projectId}",drogon::Put, "StructuraSystems::Server::JwtFilter");
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

			// The projects of the user, in the order of their id.
			std::vector<std::shared_ptr<Project>> accessible;
			for (const auto& entry : _ProjectService->getProjects())
			{
				const auto project = std::dynamic_pointer_cast<Project>(entry);
				if (project != nullptr && canAccessProject(*user, project, ProjectAccess::Read))
					accessible.push_back(project);
			}
			callback(paginate(req, accessible,
				[](const std::shared_ptr<Project>& project) { return boost::uuids::to_string(project->getId()); },
				[this](const std::shared_ptr<Project>& project) { return _ProjectService->serializeProject(project); }));
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
			// CC-006: the name is required.
			if (!requestObject["name"].isString() || isBlank(requestObject["name"].asString()))
			{
				callback(jsonError(drogon::k400BadRequest, "The project needs a name (a non-empty string)."));
				return;
			}
			if (requestObject.isMember("description") && !requestObject["description"].isNull() && !requestObject["description"].isString())
			{
				callback(jsonError(drogon::k400BadRequest, "description must be a string."));
				return;
			}
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

		/// PUT /projects/{id}: a ProjectRequest with name?, description? and defaultBranch? ({"@id": ...}).
		void updateProject(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId) const {
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto jsonObject = requireJson(request);
			if (jsonObject == nullptr || !jsonObject->isObject())
			{
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}
			const auto& requestObject = *jsonObject;

			std::string name;
			if (requestObject.isMember("name") && !requestObject["name"].isNull())
			{
				if (!requestObject["name"].isString() || isBlank(requestObject["name"].asString()))
				{
					callback(jsonError(drogon::k400BadRequest, "name must be a non-empty string."));
					return;
				}
				name = requestObject["name"].asString();
			}
			std::string description;
			if (requestObject.isMember("description") && !requestObject["description"].isNull())
			{
				if (!requestObject["description"].isString())
				{
					callback(jsonError(drogon::k400BadRequest, "description must be a string."));
					return;
				}
				description = requestObject["description"].asString();
			}

			std::shared_ptr<SysMLv2::REST::Branch> defaultBranch;
			if (requestObject.isMember("defaultBranch") && !requestObject["defaultBranch"].isNull())
			{
				const auto& reference = requestObject["defaultBranch"];
				const auto branchId = reference.isObject() && reference["@id"].isString() ? parseUuid(reference["@id"].asString()) : std::nullopt;
				if (!branchId)
				{
					callback(jsonError(drogon::k400BadRequest, "defaultBranch must be a reference {\"@id\": ...}."));
					return;
				}
				// An unknown branch, or a branch of another project, leaves the default branch as it is.
				auto branchUuid = *branchId;
				defaultBranch = ProjectVersioningService::getInstance()->getBranchById(project, branchUuid);
				if (defaultBranch == nullptr)
				{
					callback(jsonError(drogon::k404NotFound, "Branch not found."));
					return;
				}
			}

			std::shared_ptr<SysMLv2::REST::Project> updated;
			try {
				updated = _ProjectService->updateProject(project->getId(), name, description, defaultBranch);
			}
			catch (const BranchNotFoundException&) {
				// the branch was deleted after it was looked up; nothing was changed
				callback(jsonError(drogon::k404NotFound, "Branch not found."));
				return;
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
				return;
			}
			if (updated == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Project not found."));
				return;
			}

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(_ProjectService->serializeProject(updated));
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

		static bool isBlank(const std::string& value)
		{
			return value.find_first_not_of(" \t\r\n") == std::string::npos;
		}

		/// An owning group may only be set by its members and by administrators.
		static bool mayUseGroup(const User& user, const std::string& group)
		{
			return group.empty() || user.role() == USER_ROLE::GLOBAL_ADMINISTRATOR || user.group() == group;
		}
	};
}
