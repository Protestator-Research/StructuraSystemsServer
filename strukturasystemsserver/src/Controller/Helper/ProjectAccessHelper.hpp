
#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>

#include "RequestHelper.hpp"
#include "../../Entities/db/Project.hpp"
#include "../../Entities/db/User.hpp"
#include "../../Services/AuthContext.h"
#include "../../Services/AccessControlService.h"
#include "../../Services/AuthenticationService.h"
#include "../../Services/ServerProjectService.h"

namespace StructuraSystems::Server
{
	/// Returns who sends the request: the AuthContext that the JwtFilter stored in the request attributes. Without it
	/// (a route without the filter) nothing is resolved here and the answer is std::nullopt.
	inline std::optional<AuthContext> currentPrincipal(const drogon::HttpRequestPtr& request)
	{
		const auto& attributes = request->attributes();
		if (attributes->find(AUTH_CONTEXT_ATTRIBUTE))
		{
			const auto& stored = attributes->get<std::shared_ptr<const AuthContext>>(AUTH_CONTEXT_ATTRIBUTE);
			if (stored != nullptr)
				return *stored;
		}

		// No attribute: the request did not pass the JwtFilter. Fail closed, the filter is the only place that interprets tokens.
		return std::nullopt;
	}

	/// Returns the user of a login session. std::nullopt for an API token (it is not a user and must never be treated as
	/// one, least of all as an administrator) and if the user vanished in the meantime.
	inline std::optional<User> currentUser(const drogon::HttpRequestPtr& request)
	{
		const auto principal = currentPrincipal(request);
		if (!principal || !principal->isSession())
			return std::nullopt;
		return *principal->user();
	}

	/// Loads the project and checks the access of the current principal: a session by the role model (canAccessProject),
	/// an API token by canTokenAccessProject. Unknown project and denied access answer with the same 404 to avoid
	/// enumeration. On any error the callback is called and nullptr is returned. If userOut is given the route needs a
	/// user: it receives the user of the session, an API token is answered with 403 session_required.
	inline std::shared_ptr<Project> withProject(const drogon::HttpRequestPtr& request,
		const std::string& projectIdStr,
		ProjectAccess access,
		const std::function<void(const drogon::HttpResponsePtr&)>& callback,
		User* userOut = nullptr)
	{
		const auto principal = currentPrincipal(request);
		if (!principal)
		{
			callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
			return nullptr;
		}
		if (userOut != nullptr && !principal->isSession())
		{
			callback(jsonError(drogon::k403Forbidden, "session_required"));
			return nullptr;
		}

		const auto parsedProjectId = parseUuid(projectIdStr);
		if (!parsedProjectId)
		{
			callback(jsonError(drogon::k400BadRequest, "Invalid project id."));
			return nullptr;
		}

		const auto project = std::dynamic_pointer_cast<Project>(ServerProjectService::getInstance()->getProjectById(*parsedProjectId));
		const bool allowed = principal->isSession()
			? canAccessProject(*principal->user(), project, access)
			: canTokenAccessProject(*principal->apiToken(), project, access);
		if (project == nullptr || !allowed)
		{
			callback(jsonError(drogon::k404NotFound, "Project not found."));
			return nullptr;
		}

		if (userOut != nullptr)
			*userOut = *principal->user();
		return project;
	}
}
