#pragma once

#include <drogon/HttpController.h>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_serialize.hpp>
#include <utility>

#include "SystemController.hpp"
#include "../Services/AuthenticationService.h"
#include "../Entities/json/AuthenticationResponse.h"
#include "../Entities/json/UserResponse.h"
#include "../Entities/json/UserRequest.h"
#include "../Filter/JwtFilter.hpp"
#include "../Services/AccessControlService.h"
#include "../Services/LoginThrottle.h"
#include "../Services/ServerConfig.h"
#include "Helper/RequestHelper.hpp"
#include "Helper/ProjectAccessHelper.hpp"

namespace StructuraSystems::Server
{
	class AuthenticationController : public drogon::HttpController<AuthenticationController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(AuthenticationController::login, "/login", drogon::Post);
		ADD_METHOD_TO(AuthenticationController::registerUser, "/admin/users", drogon::Post, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(AuthenticationController::getUser, "/me", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(AuthenticationController::changeOwnPassword, "/me/password", drogon::Put, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(AuthenticationController::getAllUser, "/admin/users", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(AuthenticationController::deleteUser, "/admin/users/{1:username}", drogon::Delete, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(AuthenticationController::changeUser, "/admin/users/{1:username}", drogon::Put, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END

		~AuthenticationController() override = default;

		void login(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const
		{
			const auto payload = requireJson(request);
			if (!payload || !payload->isObject()) {
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}
			const auto& jsonObject = *payload;
			const std::string username = jsonObject["username"].isString() ? jsonObject["username"].asString() : std::string();
			const std::string password = jsonObject["password"].isString() ? jsonObject["password"].asString() : std::string();

			// Checked before the (expensive) password hash runs.
			auto& throttle = LoginThrottle::getInstance();
			if (const auto retryAfter = throttle.checkAttempt(clientIp(request, ServerConfig::get().TrustedProxies), username)) {
				auto response = jsonError(drogon::k429TooManyRequests, "too_many_requests");
				response->addHeader("Retry-After", std::to_string(retryAfter->count()));
				callback(response);
				return;
			}

			try {
				auto bearrierString = AuthService->authenticateUserWith(username, password);
				auto authenticationResponse = AuthenticationResponse(bearrierString);

				auto response = drogon::HttpResponse::newHttpJsonResponse(authenticationResponse.toJson());
				response->addHeader("Authorization","Bearer " + bearrierString);
				callback(response);
				return;
			}
			catch (...) {
				throttle.recordFailure(username);
				callback(drogon::HttpResponse::newHttpResponse(drogon::k401Unauthorized,drogon::CT_TEXT_PLAIN));
				return;
			}
		}

		void registerUser(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto actor = currentUser(request);
			if (!actor) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}
			if (!canListAllUsers(*actor)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}

			const auto payload = requireJson(request);
			if (!payload) {
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}
			std::shared_ptr<UserRequest> userRequest;
			try {
				userRequest = std::make_shared<UserRequest>(payload->toStyledString(), true);
			}
			catch (const UserRequestValidationException& exception) {
				callback(jsonError(drogon::k400BadRequest, exception.what()));
				return;
			}
			catch (...) {
				callback(jsonError(drogon::k400BadRequest, "Invalid user request."));
				return;
			}

			const auto role = static_cast<USER_ROLE>(userRequest->getUserRole());
			if (!canCreateUser(*actor, role)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions for this role."));
				return;
			}

			try {
				AuthService->addUser(userRequest->getUsername(), userRequest->getPassword(), role, userRequest->getGroup());
			}
			catch (const UserAlreadyExistsException&) {
				callback(jsonError(drogon::k409Conflict, "User already exists."));
				return;
			}
			catch (...) {
				callback(jsonError(drogon::k500InternalServerError, "User could not be created."));
				return;
			}
			callback(drogon::HttpResponse::newHttpResponse(drogon::k201Created,drogon::CT_TEXT_PLAIN));
		}

		void getUser(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto user = currentUser(request);
			if (!user) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}
			const auto userResponse = UserResponse(*user);
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(userResponse.serializeToJson());
			callback(response);
		}

		void changeOwnPassword(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto user = currentUser(request);
			if (!user) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}
			const auto payload = requireJson(request);
			if (!payload || !payload->isObject() ||
				!(*payload)["oldPassword"].isString() || !(*payload)["newPassword"].isString()) {
				callback(jsonError(drogon::k400BadRequest, "Expected string fields oldPassword and newPassword."));
				return;
			}
			const auto oldPassword = (*payload)["oldPassword"].asString();
			const auto newPassword = (*payload)["newPassword"].asString();
			if (!UserRequest::isValidPassword(newPassword)) {
				callback(jsonError(drogon::k400BadRequest, "password must have at least 12 characters."));
				return;
			}

			try {
				// The JwtFilter has validated the header; the current session stays valid, all others of the user are revoked.
				const auto currentToken = request->getHeader("Authorization").substr(7);
				if (!AuthService->changeOwnPassword(user->username(), oldPassword, newPassword, currentToken)) {
					callback(jsonError(drogon::k401Unauthorized, "The old password is wrong."));
					return;
				}
			}
			catch (...) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}
			Json::Value body(Json::objectValue);
			body["status"] = "password_changed";
			callback(drogon::HttpResponse::newHttpJsonResponse(body));
		}

		void getAllUser(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto actor = currentUser(request);
			if (!actor) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}

			std::vector<User> users;
			if (canListAllUsers(*actor)) {
				users = AuthService->getUsersFromDatabase();
			}
			else if (actor->role() == USER_ROLE::GROUP_MANAGER && canManageGroupMembers(*actor, actor->group())) {
				for (const auto& user : AuthService->getUsersFromDatabase())
					if (user.group() == actor->group())
						users.push_back(user);
			}
			else {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}

			std::string body = "[\r\n";
			for (size_t i = 0; i < users.size(); ++i) {
				body += UserResponse(users[i]).serializeToJson();
				if (i<users.size()-1)
					body += ",\r\n";
			}
			body += "]\r\n";

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(body);
			callback(response);
		}

		void deleteUser(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string username) const {
			const auto actor = currentUser(request);
			if (!actor) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}
			if (!canListAllUsers(*actor)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}

			const auto target = AuthService->findUser(username);
			if (!target) {
				callback(jsonError(drogon::k404NotFound, "User not found."));
				return;
			}
			if (!canDeleteUser(*actor, *target)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}
			if (target->role() == USER_ROLE::GLOBAL_ADMINISTRATOR && AuthService->countAdministrators() <= 1) {
				callback(jsonError(drogon::k409Conflict, "The last administrator cannot be deleted."));
				return;
			}

			try {
				const auto user = AuthService->deleteUserFromDatabase(std::move(username));
				const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
				response->setBody(UserResponse(user).serializeToJson());
				callback(response);
			}
			catch (const UserNotFoundException&) {
				callback(jsonError(drogon::k404NotFound, "User not found."));
			}
		}

		void changeUser(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string username) const {
			const auto actor = currentUser(request);
			if (!actor) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return;
			}
			if (!canListAllUsers(*actor)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}

			const auto target = AuthService->findUser(username);
			if (!target) {
				callback(jsonError(drogon::k404NotFound, "User not found."));
				return;
			}

			const auto payload = requireJson(request);
			if (!payload) {
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}
			std::shared_ptr<UserRequest> userRequest;
			try {
				userRequest = std::make_shared<UserRequest>(payload->toStyledString(), false);
			}
			catch (const UserRequestValidationException& exception) {
				callback(jsonError(drogon::k400BadRequest, exception.what()));
				return;
			}
			catch (...) {
				callback(jsonError(drogon::k400BadRequest, "Invalid user request."));
				return;
			}

			std::optional<USER_ROLE> newRole;
			if (userRequest->isUserRoleChanged())
				newRole = static_cast<USER_ROLE>(userRequest->getUserRole());
			std::optional<std::string> newPassword;
			if (userRequest->isPasswordChanged())
				newPassword = userRequest->getPassword();
			std::optional<std::string> newGroup;
			if (userRequest->isGroupChanged())
				newGroup = userRequest->getGroup();

			if (!canModifyUser(*actor, *target, newRole)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return;
			}
			if (target->role() == USER_ROLE::GLOBAL_ADMINISTRATOR && newRole.has_value() &&
				*newRole != USER_ROLE::GLOBAL_ADMINISTRATOR && AuthService->countAdministrators() <= 1) {
				callback(jsonError(drogon::k409Conflict, "The last administrator cannot be demoted."));
				return;
			}

			// A password set by someone else must be changed by the user at the next login.
			std::optional<bool> mustChangePassword;
			if (newPassword.has_value() && actor->username() != target->username())
				mustChangePassword = true;

			try {
				const auto user = AuthService->changeUserInDatabase(username, newPassword, newGroup, newRole, mustChangePassword);
				const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
				response->setBody(UserResponse(user).serializeToJson());
				callback(response);
			}
			catch (const UserNotFoundException&) {
				callback(jsonError(drogon::k404NotFound, "User not found."));
			}
			catch (...) {
				callback(jsonError(drogon::k500InternalServerError, "User could not be changed."));
			}
		}

		std::shared_ptr<AuthenticationService> AuthService = AuthenticationService::getInstance();
	};
}
