#include "ServerProjectService.h"

#include "../Controller/DataBaseController.h"
#include "ProjectVersioningService.h"
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
		auto project = ProjectService::createProject(projectName, description);

		ProjectVersioningService::getInstance()->addBranchFromProject(project);

		DBController->addProject(project);

		return project;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::updateProject(boost::uuids::uuid projectId,
		std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch) {
		const auto& updatedProject = ProjectService::updateProject(projectId, projectName, description, branch);
		DBController->updateProject(updatedProject);
		return updatedProject;
	}

	std::shared_ptr<SysMLv2::REST::Project> ServerProjectService::deleteProject(boost::uuids::uuid projectId) {
		const auto& deletedProject = ProjectService::deleteProject(projectId);
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
