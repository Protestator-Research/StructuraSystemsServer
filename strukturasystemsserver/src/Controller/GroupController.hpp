
#pragma once

#include <drogon/HttpController.h>

#include "../Services/AccessControlService.h"
#include "../Services/AuthenticationService.h"
#include "../Entities/json/UserResponse.h"
#include "../Entities/json/UserRequest.h"
#include "../Filter/JwtFilter.hpp"
#include "Helper/RequestHelper.hpp"
#include "Helper/ProjectAccessHelper.hpp"

namespace StructuraSystems::Server
{
	class GroupController : public drogon::HttpController<GroupController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(GroupController::getMembers, "/groups/{1:group}/members", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(GroupController::addMember, "/groups/{1:group}/members/{2:username}", drogon::Post, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(GroupController::removeMember, "/groups/{1:group}/members/{2:username}", drogon::Delete, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END

		~GroupController() override = default;

		void getMembers(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string group) const {
			const auto actor = currentUser(request);
			if (!actor) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}
			if (!canManageGroupMembers(*actor, group)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}

			std::vector<User> members;
			for (const auto& user : AuthService->getUsersFromDatabase())
				if (user.group() == group)
					members.push_back(user);

			std::string body = "[\r\n";
			for (size_t i = 0; i < members.size(); ++i) {
				body += UserResponse(members[i]).serializeToJson();
				if (i < members.size() - 1)
					body += ",\r\n";
			}
			body += "]\r\n";

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(body);
			callback(response);
		}

		void addMember(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string group, std::string username) const {
			changeMembership(request, callback, group, username, true);
		}

		void removeMember(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string group, std::string username) const {
			changeMembership(request, callback, group, username, false);
		}

	private:
		void changeMembership(const drogon::HttpRequestPtr& request, const std::function<void(const drogon::HttpResponsePtr&)>& callback, const std::string& group, const std::string& username, bool add) const {
			const auto actor = currentUser(request);
			if (!actor) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}
			if (!canManageGroupMembers(*actor, group)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}
			if (group.empty() || !UserRequest::isValidGroup(group)) {
				callback(jsonError(drogon::k400BadRequest, "Invalid group name."));
				return;
			}

			const auto target = AuthService->findUser(username);
			if (!target || (!add && target->group() != group)) {
				callback(jsonError(drogon::k404NotFound, "User not found."));
				return;
			}
			if (add ? !canAddToGroup(*actor, *target, group) : !canRemoveFromGroup(*actor, *target, group)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}

			try {
				const auto user = AuthService->changeUserInDatabase(username, std::nullopt, add ? group : std::string(), std::nullopt);
				const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
				response->setBody(UserResponse(user).serializeToJson());
				callback(response);
			}
			catch (const UserNotFoundException&) {
				callback(jsonError(drogon::k404NotFound, "User not found."));
			}
			catch (...) {
				callback(jsonError(drogon::k500InternalServerError, "Group membership could not be changed."));
			}
		}

		std::shared_ptr<AuthenticationService> AuthService = AuthenticationService::getInstance();
	};
}
