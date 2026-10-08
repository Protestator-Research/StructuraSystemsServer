#pragma once

#include <memory>
#include <optional>
#include <string>

#include "../Entities/db/User.hpp"
#include "../Entities/db/Project.hpp"

namespace StructuraSystems::Server
{
	enum class ProjectAccess { Read, Write, Delete };

	// Stateless access rules of the role model. All functions are pure; checks that
	// need global state (e.g. the number of administrators) are done by the caller.

	bool isValidRole(int role);

	bool canAccessProject(const User& user, const std::shared_ptr<Project>& project, ProjectAccess access);

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
