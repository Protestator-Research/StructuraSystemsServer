#include "ServerProjectService.h"

#include "../Controller/DataBaseController.h"
#include "ProjectVersioningService.h"
#include "../Entities/db/Project.hpp"
#include <sysmlv2/rest/entities/Project.h>

namespace StructuraSystems::Server
{
	std::shared_ptr<ServerProjectService> ServerProjectService::Instance = nullptr;

	std::shared_ptr<ServerProjectService> ServerProjectService::getInstance()
	{
		if (Instance == nullptr) {
			struct concreteServerProjectService : public ServerProjectService {};
			Instance = std::make_shared<concreteServerProjectService>();
		}

		return Instance;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::createProject(std::string projectName,
		std::string description) {
		auto project = std::make_shared<Project>(projectName,description,"main");
		ProjectMap[project->getId()] = project;

		ProjectVersioningService::getInstance()->addBranchFromProject(project);

		DBController->addProject(project);

		return project;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::createProject(std::string projectName,
		std::string description, std::string owner, std::string group)
	{
		auto project = std::make_shared<Project>(projectName,description,"main",owner,group);

		ProjectMap[project->getId()] = project;

		ProjectVersioningService::getInstance()->addBranchFromProject(project);

		DBController->addProject(project);

		return project;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::updateProject(boost::uuids::uuid projectId,
	                                                                            std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch) {
		if(ProjectMap.count(projectId)>0) {
			const auto& project = std::dynamic_pointer_cast<Project>(ProjectMap[projectId]);

			project->setName(projectName);

			if(!description.empty())
				project->setDescription(description);

			if(branch != nullptr)
				project->setDefaultBranch(branch);

			DBController->updateProject(project);

			return project;
		}

		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::deleteProject(boost::uuids::uuid projectId) {
		const auto& deletedProject = std::dynamic_pointer_cast<Project>(deleteProject(projectId));
		DBController->deleteProject(deletedProject);
		return deletedProject;
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
