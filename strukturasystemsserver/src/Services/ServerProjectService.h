#pragma once

#include <sysmlv2/service/implementation/ProjectService.h>
#include <memory>
#include <shared_mutex>
#include <vector>

namespace StructuraSystems::Server
{
	class DataBaseController;
	class Project;

	class ServerProjectService : public SysMLv2::API::ProjectService
	{
	public:
		static std::shared_ptr<ServerProjectService> getInstance();

		~ServerProjectService() override = default;

		// The base class reads the protected ProjectMap, so reads are wrapped in a shared lock.
		std::vector<std::shared_ptr<SysMLv2::REST::Project>> getProjects() override;

		std::shared_ptr<SysMLv2::REST::Project> getProjectById(boost::uuids::uuid projectId) override;

		std::shared_ptr<SysMLv2::REST::Project> createProject(std::string projectName, std::string description) override;

		std::shared_ptr<SysMLv2::REST::Project> createProject(std::string projectName, std::string description, std::string owner, std::string group);

		std::shared_ptr<SysMLv2::REST::Project> updateProject(boost::uuids::uuid projectId, std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch) override;

		std::shared_ptr<SysMLv2::REST::Project> deleteProject(boost::uuids::uuid projectId) override;
	private:
		ServerProjectService();

		/// Registers the project, creates its branch and initial commit and stores it. Takes the
		/// unique lock itself.
		std::shared_ptr<SysMLv2::REST::Project> storeNewProject(const std::shared_ptr<Project>& project);

		/// Guards ProjectMap. Lock order: this mutex before ProjectVersioningService's. Not recursive.
		mutable std::shared_mutex Mutex;

		DataBaseController* DBController;

	};
}