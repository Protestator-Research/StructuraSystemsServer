#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "../Entities/db/ApiToken.hpp"
#include "../Entities/db/User.hpp"
#include "../Entities/db/Project.hpp"

namespace StructuraSystems::Server
{
	enum class ProjectAccess { Read, Write, Delete };

	// Stateless access rules of the role model. All functions are pure; checks that
	// need global state (e.g. the number of administrators) are done by the caller.

	bool isValidRole(int role);

	bool canAccessProject(const User& user, const std::shared_ptr<Project>& project, ProjectAccess access);

	/// Result of isAllowedForApiToken.
	enum class ApiTokenPathDecision { Allowed, SessionRequired, InsufficientScope };

	/// Rules of the API tokens that depend on the route only. The method is the upper case HTTP method ("GET"), the path
	/// the request path. Pure and strict: /admin, /groups, /me (the segments, any case, also with a trailing or doubled
	/// slash) and PUT /projects/{id}/owner need a login session; a scope READ allows GET, HEAD, OPTIONS and
	/// POST /projects/{id}/query-results (fixed segments compared case insensitively, like the router). A path that is not in canonical form (no leading slash, empty or dot segments, '%',
	/// backslash, trailing slash) or an unknown method is never allowed (InsufficientScope).
	ApiTokenPathDecision isAllowedForApiToken(std::string_view method, std::string_view path, ApiTokenScope scope);

	/// May the token access the project? Write and Delete need the scope READ_WRITE, Delete also a server token, see the matrix in
	/// docs/API_TOKEN_PLAN.md 4.3. Unknown (nullptr) projects are never accessible.
	bool canTokenAccessProject(const ApiToken& token, const std::shared_ptr<Project>& project, ProjectAccess access);

	/// May the token create projects (POST /projects)? Needs READ_WRITE and a group or server target.
	bool canTokenCreateProject(const ApiToken& token);

	/// May the actor issue, list and revoke API tokens? Only a GLOBAL_ADMINISTRATOR (the caller must pass a session user).
	bool canManageApiTokens(const User& actor);

	bool canListAllUsers(const User& actor);
	bool canCreateUser(const User& actor, USER_ROLE targetRole);
	bool canModifyUser(const User& actor, const User& target, std::optional<USER_ROLE> newRole);
	bool canDeleteUser(const User& actor, const User& target);

	/// Coarse check: may the actor list and manage the members of this group at all?
	bool canManageGroupMembers(const User& actor, const std::string& group);
	/// May the actor put the target into the group (actor must pass canManageGroupMembers)?
	bool canAddToGroup(const User& actor, const User& target, const std::string& group);
	/// May the actor remove the target from the group (actor must pass canManageGroupMembers)?
	bool canRemoveFromGroup(const User& actor, const User& target, const std::string& group);
}
