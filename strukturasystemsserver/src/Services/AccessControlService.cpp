#include "AccessControlService.h"

#include <algorithm>
#include <cctype>
#include <vector>

#include <boost/uuid/uuid_io.hpp>

namespace StructuraSystems::Server
{
	namespace {
		bool isAdmin(const User& user) { return user.role() == USER_ROLE::GLOBAL_ADMINISTRATOR; }
		bool isModerator(const User& user) { return user.role() == USER_ROLE::GLOBAL_MODERATOR; }

		std::string lower(std::string_view value)
		{
			std::string result(value);
			std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return result;
		}

		/// The non-empty segments of the path, lower case. Tolerant on purpose: used to recognise the session-only routes,
		/// so that no spelling of them (//admin, /ADMIN/, /admin/./x) is mistaken for another route.
		std::vector<std::string> looseSegments(std::string_view path)
		{
			std::vector<std::string> segments;
			std::size_t position = 0;
			while (position <= path.size()) {
				auto end = path.find('/', position);
				if (end == std::string_view::npos)
					end = path.size();
				if (end > position)
					segments.push_back(lower(path.substr(position, end - position)));
				position = end + 1;
			}
			return segments;
		}

		/// Strict form: '/' followed by non-empty segments separated by single slashes, no trailing slash, no "." or "..",
		/// no '%' (the framework has decoded the path already, a remaining one is suspicious) and no backslash or control characters.
		bool isCanonicalPath(std::string_view path)
		{
			if (path.size() < 2 || path.front() != '/' || path.back() == '/')
				return false;
			std::size_t position = 1;
			while (position <= path.size()) {
				auto end = path.find('/', position);
				if (end == std::string_view::npos)
					end = path.size();
				const auto segment = path.substr(position, end - position);
				if (segment.empty() || segment == "." || segment == "..")
					return false;
				position = end + 1;
			}
			return std::none_of(path.begin(), path.end(), [](unsigned char c) { return c == '%' || c == '\\' || c < 0x20 || c == 0x7f; });
		}
	}

	bool isValidRole(int role)
	{
		return role == USER_ROLE::USER || role == USER_ROLE::GROUP_MANAGER ||
			role == USER_ROLE::GLOBAL_MODERATOR || role == USER_ROLE::GLOBAL_ADMINISTRATOR;
	}

	bool canAccessProject(const User& user, const std::shared_ptr<Project>& project, ProjectAccess access)
	{
		if (project == nullptr)
			return false;
		if (isAdmin(user))
			return true;
		if (project->isLibrary())
			return false;

		const auto owner = project->getOwner();
		if (!owner.empty() && owner == user.username())
			return true;

		if (access == ProjectAccess::Delete)
			return false;

		// Projects without an owner are admin-only unless a group owns them.
		const auto owningGroup = project->getOwningGroup();
		return !owningGroup.empty() && owningGroup == user.group();
	}

	ApiTokenPathDecision isAllowedForApiToken(std::string_view method, std::string_view path, ApiTokenScope scope)
	{
		const bool readOnlyMethod = method == "GET" || method == "HEAD" || method == "OPTIONS";
		const bool knownMethod = readOnlyMethod || method == "POST" || method == "PUT" || method == "DELETE" || method == "PATCH";

		const auto loose = looseSegments(path);
		if (!loose.empty() && (loose[0] == "admin" || loose[0] == "groups" || loose[0] == "me"))
			return ApiTokenPathDecision::SessionRequired;
		if (method == "PUT" && loose.size() == 3 && loose[0] == "projects" && loose[2] == "owner")
			return ApiTokenPathDecision::SessionRequired;

		// In doubt the token is denied.
		if (!knownMethod || !isCanonicalPath(path))
			return ApiTokenPathDecision::InsufficientScope;

		if (scope == ApiTokenScope::READ_WRITE || readOnlyMethod)
			return ApiTokenPathDecision::Allowed;

		// The one writing method of a read token: the (read only) execution of a query. The framework routes case
		// insensitively, so the two fixed segments are compared that way too (loose is lower case); the path is canonical here.
		if (scope == ApiTokenScope::READ && method == "POST" && loose.size() == 3 && loose[0] == "projects" && loose[2] == "query-results")
			return ApiTokenPathDecision::Allowed;
		return ApiTokenPathDecision::InsufficientScope;
	}

	bool canTokenAccessProject(const ApiToken& token, const std::shared_ptr<Project>& project, ProjectAccess access)
	{
		if (project == nullptr)
			return false;
		if (access != ProjectAccess::Read && token.scope() != ApiTokenScope::READ_WRITE)
			return false;

		switch (token.targetType()) {
			case ApiTokenTarget::PROJECT: {
				// Library projects are for server tokens only, also if one is listed.
				if (access == ProjectAccess::Delete || project->isLibrary())
					return false;
				const auto id = boost::uuids::to_string(project->getId());
				return std::find(token.projects().begin(), token.projects().end(), id) != token.projects().end();
			}
			case ApiTokenTarget::GROUP:
				if (access == ProjectAccess::Delete || project->isLibrary())
					return false;
				return !token.group().empty() && project->getOwningGroup() == token.group();
			case ApiTokenTarget::SERVER:
				return true;
		}
		return false;
	}

	bool canTokenCreateProject(const ApiToken& token)
	{
		return token.scope() == ApiTokenScope::READ_WRITE && token.targetType() != ApiTokenTarget::PROJECT;
	}

	bool canManageApiTokens(const User& actor)
	{
		return isAdmin(actor);
	}

	bool canListAllUsers(const User& actor)
	{
		return isAdmin(actor) || isModerator(actor);
	}

	bool canCreateUser(const User& actor, USER_ROLE targetRole)
	{
		if (isAdmin(actor))
			return true;
		return isModerator(actor) && targetRole <= USER_ROLE::GROUP_MANAGER;
	}

	bool canModifyUser(const User& actor, const User& target, std::optional<USER_ROLE> newRole)
	{
		if (isAdmin(actor))
			return true;
		if (!isModerator(actor))
			return false;
		if (target.role() >= USER_ROLE::GLOBAL_MODERATOR)
			return false;
		return !newRole.has_value() || *newRole <= USER_ROLE::GROUP_MANAGER;
	}

	bool canDeleteUser(const User& actor, const User& target)
	{
		if (actor.username() == target.username())
			return false;
		if (isAdmin(actor))
			return true;
		return isModerator(actor) && target.role() < USER_ROLE::GLOBAL_MODERATOR;
	}

	bool canManageGroupMembers(const User& actor, const std::string& group)
	{
		if (isAdmin(actor) || isModerator(actor))
			return true;
		return actor.role() == USER_ROLE::GROUP_MANAGER && !group.empty() && actor.group() == group;
	}

	bool canAddToGroup(const User& actor, const User& target, const std::string& group)
	{
		if (!canManageGroupMembers(actor, group))
			return false;
		if (isAdmin(actor))
			return true;
		if (isModerator(actor))
			return target.role() < USER_ROLE::GLOBAL_MODERATOR;
		return target.role() == USER_ROLE::USER && target.group().empty();
	}

	bool canRemoveFromGroup(const User& actor, const User& target, const std::string& group)
	{
		if (!canManageGroupMembers(actor, group))
			return false;
		if (isAdmin(actor))
			return true;
		if (isModerator(actor))
			return target.role() < USER_ROLE::GLOBAL_MODERATOR;
		return target.role() <= USER_ROLE::GROUP_MANAGER;
	}
}
