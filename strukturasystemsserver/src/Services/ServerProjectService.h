#pragma once

#include <sysmlv2/service/implementation/ProjectService.h>
#include <vector>

namespace StructuraSystems::Server
{
	class DataBaseController;

	class ServerProjectService : public SysMLv2::API::ProjectService
	{
	public:
		static std::shared_ptr<ServerProjectService> getInstance();

		~ServerProjectService() override = default;

		std::shared_ptr<SysMLv2::REST::Project> createProject(std::string projectName, std::string description) override;

		std::shared_ptr<SysMLv2::REST::Project> createProject(std::string projectName, std::string description, std::string owner, std::string group);

		std::shared_ptr<SysMLv2::REST::Project> updateProject(boost::uuids::uuid projectId, std::string projectName, std::string description, std::shared_ptr<SysMLv2::REST::Branch> branch) override;

		std::shared_ptr<SysMLv2::REST::Project> deleteProject(boost::uuids::uuid projectId) override;
	private:
		ServerProjectService();
		static std::shared_ptr<ServerProjectService> Instance;

		DataBaseController* DBController;

	};
}