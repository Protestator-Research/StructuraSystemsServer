#include "ServerProjectService.h"

#include "../Controller/DataBaseController.h"
#include "ProjectVersioningService.h"
#include "QueryService.h"
#include "../Entities/db/Project.hpp"
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Project.h>

#include <mutex>
#include <shared_mutex>

namespace StructuraSystems::Server
{
	std::shared_ptr<ServerProjectService> ServerProjectService::getInstance()
	{
		struct concreteServerProjectService : public ServerProjectService {};
		// A function-local static is initialised exactly once, even with concurrent first calls.
		static const std::shared_ptr<ServerProjectService> instance = std::make_shared<concreteServerProjectService>();
		return instance;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Project>> ServerProjectService::getProjects()
	{
		std::shared_lock lock(Mutex);
		return ProjectService::getProjects();
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::getProjectById(boost::uuids::uuid projectId)
	{
		std::shared_lock lock(Mutex);
		return ProjectService::getProjectById(projectId);
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::createProject(std::string projectName,
		std::string description) {
		auto project = std::make_shared<Project>(projectName,description,"main");
		return storeNewProject(project);
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::storeNewProject(const std::shared_ptr<Project>& project)
	{
		// Lock order: ServerProjectService before ProjectVersioningService (taken inside addBranchFromProject).
		std::unique_lock lock(Mutex);
		ProjectMap[project->getId()] = project;

		project->getDefaultBranch()->setOwningProject(project);
		ProjectVersioningService::getInstance()->addBranchFromProject(project);

		DBController->addProject(project);

		return project;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::createProject(std::string projectName,
		std::string description, std::string owner, std::string group)
	{
		auto project = std::make_shared<Project>(projectName,description,"main",owner,group);
		return storeNewProject(project);
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::updateProject(boost::uuids::uuid projectId,
	                                                                            std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch) {
		std::unique_lock lock(Mutex);
		const auto found = ProjectMap.find(projectId);
		if (found == ProjectMap.end())
			return nullptr;

		const auto project = std::dynamic_pointer_cast<Project>(found->second);
		const auto oldName = project->getName();
		const auto oldDescription = project->getDescription();
		std::shared_ptr<SysMLv2::REST::Branch> oldDefaultBranch;
		bool defaultBranchChanged = false;

		try {
			// The default branch first: whether the branch exists and the change happen under the lock of the versioning service
			// (the only other reader of the field), and an unknown branch leaves everything as it was.
			if (branch != nullptr)
			{
				oldDefaultBranch = ProjectVersioningService::getInstance()->replaceDefaultBranch(project, branch->getId());
				defaultBranchChanged = true;
			}

			// An empty name means that the name is not changed.
			if(!projectName.empty())
				project->setName(projectName);

			if(!description.empty())
				project->setDescription(description);

			DBController->updateProject(project);
		}
		catch (...) {
			// Memory follows the database: put the old state back.
			project->setName(oldName);
			std::string restoredDescription = oldDescription;
			project->setDescription(restoredDescription);
			if (defaultBranchChanged)
				ProjectVersioningService::getInstance()->restoreDefaultBranch(project, oldDefaultBranch);
			throw;
		}

		return project;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::deleteProject(boost::uuids::uuid projectId) {
		std::unique_lock lock(Mutex);
		const auto baseDeleted = SysMLv2::API::ProjectService::deleteProject(projectId);
		if (baseDeleted == nullptr)
			return nullptr;

		const auto deletedProject = std::dynamic_pointer_cast<Project>(baseDeleted);
		try {
			DBController->deleteProject(deletedProject);
		}
		catch (...) {
			// The database kept the project, so memory keeps it as well.
			ProjectMap[projectId] = deletedProject;
			throw;
		}
		// Lock order: ServerProjectService before QueryService and ProjectVersioningService. The queries of a deleted project go
		// with it; the versioning service lets go of its commits (cache and index), branches and tags.
		QueryService::getInstance()->deleteQueriesOfProject(deletedProject->getId());
		ProjectVersioningService::getInstance()->forgetProject(deletedProject);
		return deletedProject;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::changeOwnership(boost::uuids::uuid projectId,
		std::optional<std::string> owner, std::optional<std::string> owningGroup)
	{
		std::unique_lock lock(Mutex);
		const auto found = ProjectMap.find(projectId);
		if (found == ProjectMap.end())
			return nullptr;

		const auto project = std::dynamic_pointer_cast<Project>(found->second);
		const auto oldOwner = project->getOwner();
		const auto oldGroup = project->getOwningGroup();
		project->setOwnership(owner.value_or(oldOwner), owningGroup.value_or(oldGroup));
		try {
			DBController->updateProject(project);
		}
		catch (...) {
			project->setOwnership(oldOwner, oldGroup);
			throw;
		}
		return project;
	}

	std::string ServerProjectService::serializeProject(const std::shared_ptr<SysMLv2::REST::Project>& project)
	{
		std::shared_lock lock(Mutex);
		return project->serializeToJson();
	}

	std::string ServerProjectService::serializeProjects(const std::function<bool(const std::shared_ptr<Project>&)>& filter)
	{
		std::shared_lock lock(Mutex);
		std::string result = "[\r\n";
		bool first = true;
		for (const auto& entry : ProjectService::getProjects())
		{
			if (!filter(std::dynamic_pointer_cast<Project>(entry)))
				continue;
			if (!first)
				result += ",\r\n";
			result += entry->serializeToJson();
			first = false;
		}
		result += "]";
		return result;
	}

	ServerProjectService::ServerProjectService() : ProjectService()
	{
		DBController = DataBaseController::getInstance();
		const auto projects = DBController->getAllProjects();
		for (const auto& project : projects)
		{
			ProjectMap.insert(std::make_pair(project->getId(), project));
		}
	}
}
