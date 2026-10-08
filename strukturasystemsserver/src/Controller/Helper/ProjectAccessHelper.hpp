
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
#include "../../Services/AccessControlService.h"
#include "../../Services/AuthenticationService.h"
#include "../../Services/ServerProjectService.h"

namespace StructuraSystems::Server
{
	/// Returns the authenticated user of the request. The JwtFilter has already validated the
	/// token; std::nullopt is only returned if the user vanished in the meantime.
	inline std::optional<User> currentUser(const drogon::HttpRequestPtr& request)
	{
		const auto& authHeader = request->getHeader("Authorization");
		if (authHeader.size() <= 7 || authHeader.compare(0, 7, "Bearer ") != 0)
			return std::nullopt;
		return AuthenticationService::getInstance()->getUserFromDatabase(authHeader.substr(7));
	}

	/// Loads the project and checks the access of the current user. Unknown project and denied
	/// access answer with the same 404 to avoid enumeration. On any error the callback is called
	/// and nullptr is returned. If userOut is given it receives the current user.
	inline std::shared_ptr<Project> withProject(const drogon::HttpRequestPtr& request,
		const std::string& projectIdStr,
		ProjectAccess access,
		const std::function<void(const drogon::HttpResponsePtr&)>& callback,
		User* userOut = nullptr)
	{
		const auto user = currentUser(request);
		if (!user)
		{
			callback(jsonError(drogon::k401Unauthorized, "Not authenticated."));
			return nullptr;
		}

		const auto parsedProjectId = parseUuid(projectIdStr);
		if (!parsedProjectId)
		{
			callback(jsonError(drogon::k400BadRequest, "Invalid project id."));
			return nullptr;
		}

		const auto project = std::dynamic_pointer_cast<Project>(ServerProjectService::getInstance()->getProjectById(*parsedProjectId));
		if (project == nullptr || !canAccessProject(*user, project, access))
		{
			callback(jsonError(drogon::k404NotFound, "Project not found."));
			return nullptr;
		}

		if (userOut != nullptr)
			*userOut = *user;
		return project;
	}
}
