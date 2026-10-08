#include "AccessControlService.h"

namespace StructuraSystems::Server
{
	namespace {
		bool isAdmin(const User& user) { return user.role() == USER_ROLE::GLOBAL_ADMINISTRATOR; }
		bool isModerator(const User& user) { return user.role() == USER_ROLE::GLOBAL_MODERATOR; }
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
