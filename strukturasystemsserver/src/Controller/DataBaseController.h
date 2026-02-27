#pragma once

#include <string>
#include <memory>
#include <vector>
#include <map>
#include <boost/uuid/uuid.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/database.hpp>
#include "../Entities/db/User.hpp"

namespace SysMLv2::REST
{
	class Data;
}

namespace SysMLv2::REST
{
	class DataVersion;
}

namespace SysMLv2::REST
{
	class Project;
	class Branch;
	class Commit;
}

namespace KerML::Entities {
	class Element;
}

namespace StructuraSystems::Server
{
	class DataBaseController
	{
	public:
		DataBaseController() = delete;
		static DataBaseController* createInstance(std::string dbAddress, std::string username, std::string password);
		static DataBaseController* getInstance();


		std::vector<std::shared_ptr<SysMLv2::REST::Project>> getAllProjects();
		void addMultibleProjects(std::vector<std::shared_ptr<SysMLv2::REST::Project>> projects);
		void addProject(std::shared_ptr<SysMLv2::REST::Project> project);
		void updateProject(std::shared_ptr<SysMLv2::REST::Project> project);
		bool deleteProject(std::shared_ptr<SysMLv2::REST::Project> project);

		void addMultibleBranches(std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>> projectBranchMap);
		void addBranch(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::Branch> branch);
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>> getAllBranches();

		void addMultibleCommits(std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> projectCommitMap);
		void addCommit(std::shared_ptr<SysMLv2::REST::Commit> commit);
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> getAllCommits();

		void addMultibleDataVersions(std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::DataVersion>> commitIdDataVersions);
		void addDataVersion(boost::uuids::uuid commitId, std::shared_ptr<SysMLv2::REST::DataVersion> dataVersion);
		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::DataVersion>> getAllDataVersions();

		void addMultibleElements(std::map<boost::uuids::uuid, std::shared_ptr<KerML::Entities::Element>> projectIDElementData);
		void addElement(boost::uuids::uuid projectId, std::shared_ptr<KerML::Entities::Element> elementData);
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<KerML::Entities::Element>>> getAllElements();

		void addUser(const User& user);
		std::map<std::string, User> getAllUser();
		void removeUser(std::string username);
		void changeUser(const User& user);

	private:
		DataBaseController(std::string dBAddress, std::string username, std::string password);
 
		static DataBaseController* Instance;

		mongocxx::instance inst;
		mongocxx::uri uri;
		mongocxx::client client;
		mongocxx::database database;

		void initializeDatabaseIfNotAvailable();

		void deleteDatabaseIfDebug();
		bool replace(std::string& str, const std::string& from, const std::string& to);
	};
}

