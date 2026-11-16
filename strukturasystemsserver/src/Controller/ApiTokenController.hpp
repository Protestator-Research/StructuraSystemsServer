#pragma once

#include <algorithm>
#include <iostream>
#include <optional>

#include <drogon/HttpController.h>
#include <boost/uuid/uuid_io.hpp>

#include "../Services/AccessControlService.h"
#include "../Services/AuthenticationService.h"
#include "../Services/ServerConfig.h"
#include "../Services/ServerProjectService.h"
#include "../Entities/db/Project.hpp"
#include "../Entities/json/ApiTokenRequest.h"
#include "../Entities/json/ApiTokenResponse.h"
#include "../Filter/JwtFilter.hpp"
#include "Helper/RequestHelper.hpp"
#include "Helper/ProjectAccessHelper.hpp"

namespace StructuraSystems::Server
{
	/// Management of API tokens, only for the login session of a GLOBAL_ADMINISTRATOR (docs/API_TOKEN_PLAN.md 4.4).
	class ApiTokenController : public drogon::HttpController<ApiTokenController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(ApiTokenController::createToken, "/admin/tokens", drogon::Post, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ApiTokenController::listTokens, "/admin/tokens", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ApiTokenController::getToken, "/admin/tokens/{1:id}", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(ApiTokenController::revokeToken, "/admin/tokens/{1:id}", drogon::Delete, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END

		~ApiTokenController() override = default;

		void createToken(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			const auto actor = authorise(request, callback);
			if (!actor)
				return;

			const auto& config = ServerConfig::get();
			const auto maxDays = std::min<std::int64_t>(config.ApiTokenMaxLifetimeDays, 365);
			std::optional<ApiTokenRequest> tokenRequest;
			try {
				tokenRequest.emplace(std::string(request->getBody()), maxDays);
			}
			catch (const ApiTokenRequestValidationException& e) {
				callback(jsonError(drogon::k400BadRequest, e.what()));
				return;
			}

			// Every project must exist and must not be a library project. No lock is held while the token store is called.
			for (const auto& projectId : tokenRequest->projects()) {
				const auto parsed = parseUuid(projectId);
				const auto project = parsed ? std::dynamic_pointer_cast<Project>(ServerProjectService::getInstance()->getProjectById(*parsed)) : nullptr;
				if (project == nullptr) {
					callback(jsonError(drogon::k400BadRequest, "Unknown project: " + projectId));
					return;
				}
				if (project->isLibrary()) {
					callback(jsonError(drogon::k400BadRequest, "Library projects can not be used for API tokens: " + projectId));
					return;
				}
			}

			// Set once the token exists: from then on a failure must not leave a token whose plaintext nobody has.
			std::string createdId;
			drogon::HttpResponsePtr response;
			try {
				const auto created = AuthService->createApiToken(tokenRequest->name(), tokenRequest->targetType(), tokenRequest->projects(),
					tokenRequest->group(), tokenRequest->scope(), actor->username(), tokenRequest->expiresInDays());

				createdId = created.Token.id();

				// A project can be deleted between the check above and the insert; its deletion hook may then have run before the
				// token existed. So look again: if any project vanished, the token is revoked (no silently shrunk token).
				std::string vanished;
				for (const auto& projectId : tokenRequest->projects()) {
					const auto parsed = parseUuid(projectId);
					if (parsed && std::dynamic_pointer_cast<Project>(ServerProjectService::getInstance()->getProjectById(*parsed)) != nullptr)
						continue;
					vanished = projectId;
					break;
				}
				if (!vanished.empty()) {
					revokeQuietly(createdId);
					createdId.clear();
					response = jsonError(drogon::k400BadRequest, "Unknown project: " + vanished);
				}
				else {

					// Audit log: never the plaintext and never the hash.
					const auto& token = created.Token;
					std::cout << "[audit] API token created: id=" << token.id() << " name=\"" << token.name() << "\" target=" << describeTarget(token)
						<< " scope=" << toString(token.scope()) << " expiresAt=" << token.expiresAt() << " by=" << actor->username() << std::endl;

					response = drogon::HttpResponse::newHttpResponse(drogon::k201Created, drogon::CT_APPLICATION_JSON);
					// The plaintext is shown once: no cache (browser, proxy) may keep the answer.
					response->addHeader("Cache-Control", "no-store");
					response->setBody(ApiTokenResponse(token, AuthService->isApiTokenExpired(token), created.Plaintext).serializeToJson());
				}
			}
			catch (const ApiTokenNameTakenException&) {
				callback(jsonError(drogon::k409Conflict, "An API token with this name already exists, or the name is held by a stored token document that could not be loaded or by a revoked token whose database delete is still pending (see the server log)."));
				return;
			}
			catch (const ApiTokenLimitException&) {
				callback(jsonError(drogon::k409Conflict, "The maximum number of API tokens is reached."));
				return;
			}
			catch (const std::invalid_argument& e) {
				// Input was validated above, so this is unexpected; still not a 500.
				if (!createdId.empty())
					revokeQuietly(createdId);
				std::cerr << "[error] API token rejected by the service: " << e.what() << std::endl;
				callback(jsonError(drogon::k400BadRequest, "Invalid API token request."));
				return;
			}
			catch (const std::exception& e) {
				if (!createdId.empty())
					revokeQuietly(createdId);
				callback(internalError(e));
				return;
			}
			// Only here, outside the try: a throwing callback can neither revoke a delivered token nor lead to a second answer.
			callback(response);
		}

		void listTokens(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback) const {
			if (!authorise(request, callback))
				return;
			try {
				const auto tokens = AuthService->listApiTokens();
				callback(paginate(request, tokens,
					[](const ApiToken& token) { return token.id(); },
					[this](const ApiToken& token) { return ApiTokenResponse(token, AuthService->isApiTokenExpired(token)).serializeToJson(); }));
			}
			catch (const std::exception& e) {
				callback(internalError(e));
			}
		}

		void getToken(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string id) const {
			if (!authorise(request, callback))
				return;
			try {
				const auto token = findToken(id);
				if (!token) {
					callback(jsonError(drogon::k404NotFound, "API token not found."));
					return;
				}
				const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
				response->setBody(ApiTokenResponse(*token, AuthService->isApiTokenExpired(*token)).serializeToJson());
				callback(response);
			}
			catch (const std::exception& e) {
				callback(internalError(e));
			}
		}

		void revokeToken(const drogon::HttpRequestPtr& request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string id) const {
			const auto actor = authorise(request, callback);
			if (!actor)
				return;
			try {
				// Read first for the audit line (absent if only the database delete of an earlier revoke is pending); revoke decides.
				const auto parsed = parseUuid(id);
				const auto canonicalId = parsed ? boost::uuids::to_string(*parsed) : std::string();
				const auto token = canonicalId.empty() ? std::nullopt : AuthService->findApiToken(canonicalId);
				const auto result = canonicalId.empty() ? ApiTokenRevokeResult::NotFound : AuthService->revokeApiToken(canonicalId);
				if (result == ApiTokenRevokeResult::NotFound) {
					callback(jsonError(drogon::k404NotFound, "API token not found."));
					return;
				}
				if (token)
					std::cout << "[audit] API token revoked: id=" << token->id() << " name=\"" << token->name() << "\" target=" << describeTarget(*token)
						<< " scope=" << toString(token->scope()) << " expiresAt=" << token->expiresAt() << " by=" << actor->username() << std::endl;
				else
					std::cout << "[audit] API token revocation: database delete retried: id=" << canonicalId << " by=" << actor->username() << std::endl;
				if (result == ApiTokenRevokeResult::RevokedDatabasePending) {
					callback(jsonError(drogon::k503ServiceUnavailable, "Token revoked for this server process; the database delete failed and will be retried."));
					return;
				}
				callback(drogon::HttpResponse::newHttpResponse(drogon::k204NoContent, drogon::CT_NONE));
			}
			catch (const std::exception& e) {
				callback(internalError(e));
			}
		}

	private:
		/// The session user if he may manage API tokens; otherwise the answer (401 or 403) is sent and std::nullopt returned.
		static std::optional<User> authorise(const drogon::HttpRequestPtr& request, const std::function<void(const drogon::HttpResponsePtr&)>& callback) {
			const auto actor = currentUser(request);
			if (!actor) {
				callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
				return std::nullopt;
			}
			// Defence in depth: the JwtFilter already answers this, the controller does not rely on it.
			if (actor->mustChangePassword()) {
				callback(jsonError(drogon::k403Forbidden, "password_change_required"));
				return std::nullopt;
			}
			if (!canManageApiTokens(*actor)) {
				callback(jsonError(drogon::k403Forbidden, "Insufficient permissions."));
				return std::nullopt;
			}
			return actor;
		}

		/// Looks the token up by an id in any spelling of a UUID (upper case, braces); the stored ids are canonical.
		std::optional<ApiToken> findToken(const std::string& id) const {
			const auto parsed = parseUuid(id);
			if (!parsed)
				return std::nullopt;
			return AuthService->findApiToken(boost::uuids::to_string(*parsed));
		}

		/// Revokes a just created token after a failure; never throws.
		void revokeQuietly(const std::string& id) const {
			try {
				AuthService->revokeApiToken(id);
			}
			catch (const std::exception& e) {
				std::cerr << "[error] API token " << id << " could not be revoked after a failed creation: " << e.what() << std::endl;
			}
		}

		static std::string describeTarget(const ApiToken& token) {
			std::string target = toString(token.targetType());
			if (token.targetType() == ApiTokenTarget::GROUP)
				target += ":" + token.group();
			else if (token.targetType() == ApiTokenTarget::PROJECT) {
				target += ":";
				for (std::size_t i = 0; i < token.projects().size(); ++i)
					target += (i > 0 ? "," : "") + token.projects()[i];
			}
			return target;
		}

		std::shared_ptr<AuthenticationService> AuthService = AuthenticationService::getInstance();
	};
}
