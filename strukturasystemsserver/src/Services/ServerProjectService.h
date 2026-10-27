#pragma once

#include <sysmlv2/service/implementation/ProjectService.h>
#include <functional>
#include <memory>
#include <optional>
#include <string>
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

		/// Changes owner and/or owning group (std::nullopt keeps the value) and stores the project.
		/// Returns nullptr if the project no longer exists.
		std::shared_ptr<SysMLv2::REST::Project> changeOwnership(boost::uuids::uuid projectId, std::optional<std::string> owner, std::optional<std::string> owningGroup);

		/// Serializes the project under the shared lock, so it cannot race with updateProject.
		std::string serializeProject(const std::shared_ptr<SysMLv2::REST::Project>& project);

		/// JSON array of all projects for which the filter returns true. Filter and serialization run
		/// under the shared lock; the filter must not call back into this service.
		std::string serializeProjects(const std::function<bool(const std::shared_ptr<Project>&)>& filter);
	private:
		ServerProjectService();

		/// Registers the project, creates its default branch (without head, a new project has no commit) and stores it. Takes the
		/// unique lock itself.
		std::shared_ptr<SysMLv2::REST::Project> storeNewProject(const std::shared_ptr<Project>& project);

		/// Guards ProjectMap. Lock order: this mutex before ProjectVersioningService's. Not recursive.
		mutable std::shared_mutex Mutex;

		DataBaseController* DBController;

	};
}