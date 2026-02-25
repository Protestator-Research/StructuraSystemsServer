#pragma once

#include <drogon/HttpController.h>
#include <boost/lexical_cast.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_serialize.hpp>

#include "SystemController.hpp"
#include "../Services/AuthenticationService.h"
#include "../Entities/json/AuthenticationResponse.h"
#include "../Services/AdministrationFilter.hpp"

namespace StructuraSystems::Server
{
	class AuthenticationController : public drogon::HttpController<AuthenticationController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(AuthenticationController::login, "/login", drogon::Post);
		ADD_METHOD_TO(AuthenticationController::login, "/register", drogon::Post, "StructuraSystems::Server::AdministrationFilter");
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
				const auto& jsonObject = *payload;
				AuthService->addUser(jsonObject["username"].asString(), jsonObject["password"].asString());

				auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created,drogon::CT_TEXT_PLAIN);
				callback(response);
				return;
			}
			catch (...) {
				callback(drogon::HttpResponse::newHttpResponse(drogon::k409Conflict,drogon::CT_TEXT_PLAIN));
				return;
			}
		}

		std::shared_ptr<AuthenticationService> AuthService = AuthenticationService::getInstance();
	};
}