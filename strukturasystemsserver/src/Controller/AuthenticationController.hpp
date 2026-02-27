#pragma once

#include <drogon/HttpController.h>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_serialize.hpp>

#include "SystemController.hpp"
#include "../Services/AuthenticationService.h"
#include "../Entities/json/AuthenticationResponse.h"
#include "../Entities/json/UserResponse.h"
#include "../Entities/json/UserRequest.h"
#include "../Filter/AdministrationFilter.hpp"

namespace StructuraSystems::Server
{
	class AuthenticationController : public drogon::HttpController<AuthenticationController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(AuthenticationController::login, "/login", drogon::Post);
		ADD_METHOD_TO(AuthenticationController::registerUser, "/admin/users", drogon::Post, "StructuraSystems::Server::AdministrationFilter");
		ADD_METHOD_TO(AuthenticationController::getUser, "/me", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(AuthenticationController::getAllUser, "/admin/users", drogon::Get, "StructuraSystems::Server::AdministrationFilter");
		ADD_METHOD_TO(AuthenticationController::deleteUser, "/admin/users/{1:username}", drogon::Delete, "StructuraSystems::Server::AdministrationFilter");
		// ADD_METHOD_TO(AuthenticationController::deleteUser, "/admin/users/{1:username}", drogon::Put, "StructuraSystems::Server::AdministrationFilter");
		METHOD_LIST_END

		~AuthenticationController() override = default;

		void login(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback)
		{
			const auto payload = request->getJsonObject();
			if (!payload) {
				callback(drogon::HttpResponse::newHttpResponse(drogon::k400BadRequest, drogon::CT_TEXT_PLAIN));
				return;
			}
			try {
				const auto& jsonObject = *payload;
				auto bearrierString = AuthService->authenticateUserWith(jsonObject["username"].asString(), jsonObject["password"].asString());
				auto authenticationResponse = AuthenticationResponse(bearrierString);

				auto response = drogon::HttpResponse::newHttpJsonResponse(authenticationResponse.toJson());
				response->addHeader("Authorization","Bearer " + bearrierString);
				callback(response);
				return;
			}
			catch (...) {
				callback(drogon::HttpResponse::newHttpResponse(drogon::k401Unauthorized,drogon::CT_TEXT_PLAIN));
				return;
			}
		}

		void registerUser(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
			const auto payload = request->getJsonObject();
			if (!payload) {
				callback(drogon::HttpResponse::newHttpResponse(drogon::k400BadRequest, drogon::CT_TEXT_PLAIN));
				return;
			}
			try {
				auto reqUser = UserRequest(payload->toStyledString());
				AuthService->addUser(reqUser.getUsername(),reqUser.getPassword(),(USER_ROLE)reqUser.getUserRole());

				auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created,drogon::CT_TEXT_PLAIN);
				callback(response);
				return;
			}
			catch (...) {
				callback(drogon::HttpResponse::newHttpResponse(drogon::k409Conflict,drogon::CT_TEXT_PLAIN));
				return;
			}
		}

		void getUser(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
			const auto authenticationHeader = request->getHeader("Authorization");
			const auto& user = AuthService->getUserFromDatabase(authenticationHeader.substr(7));
			const auto& userResponse = UserResponse(user);
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(userResponse.serializeToJson());
			callback(response);
		}

		void getAllUser(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
			const auto& users = AuthService->getUsersFromDatabase();

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

		void deleteUser(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string username) {
			const auto& user = AuthService->deleteUserFromDatabase(username);
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(UserResponse(user).serializeToJson());
			callback(response);
		}

		// void changeUser(const drogon::HttpRequestPtr&, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string username) {

		// }

		std::shared_ptr<AuthenticationService> AuthService = AuthenticationService::getInstance();
	};
}