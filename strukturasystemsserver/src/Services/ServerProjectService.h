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

		std::shared_ptr<SysMLv2::REST::Project> createProject(std::string projectName, std::string description = "") override;


	private:
		ServerProjectService();
		static std::shared_ptr<ServerProjectService> Instance;

		DataBaseController* DBController;

	};
}