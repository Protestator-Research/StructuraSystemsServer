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
	class Project;
	class Branch;
	class Commit;
	class DataVersion;
	class Tag;
	class Data;

}

namespace KerML::Entities {
	class Element;
}

namespace StructuraSystems::Server
{
	class TwinResponse;

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
		void updateBranch(std::shared_ptr<SysMLv2::REST::Branch> branch);
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>> getAllBranches();
		void deleteBranch(std::shared_ptr<SysMLv2::REST::Branch> branch);

		void addMultibleCommits(std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> projectCommitMap);
		void addCommit(std::shared_ptr<SysMLv2::REST::Commit> commit);
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> getAllCommits();

		void addMultibleDataVersions(std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::DataVersion>> commitIdDataVersions);
		void addDataVersion(boost::uuids::uuid commitId, std::shared_ptr<SysMLv2::REST::DataVersion> dataVersion);
		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::DataVersion>> getAllDataVersions();

		void addMultibleElements(std::map<boost::uuids::uuid, std::shared_ptr<KerML::Entities::Element>> projectIDElementData);
		void addElement(boost::uuids::uuid projectId, std::shared_ptr<KerML::Entities::Element> elementData);
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<KerML::Entities::Element>>> getAllElements();

		void addTag(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::Tag> tag);
		void deleteTag(std::shared_ptr<SysMLv2::REST::Tag> tag);
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Tag>>> getAllTags();

		void addTwin(boost::uuids::uuid projectId, std::shared_ptr<StructuraSystems::Server::TwinResponse> twinTag);
		void deleteTwin(std::shared_ptr<StructuraSystems::Server::TwinResponse> twinTag);
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<StructuraSystems::Server::TwinResponse>>> getAllTwins();

		void addUser(const User& user);
		std::map<std::string, User> getAllUser();
		void removeUser(std::string username);
		void changeUser(const User& user);

	private:
		DataBaseController(std::string dBAddress, std::string username, std::string password);

		void addVersionToDatabase();
 
		static DataBaseController* Instance;

		mongocxx::instance inst;
		mongocxx::uri uri;
		mongocxx::client client;
		mongocxx::database database;

		void initializeDatabaseIfNotAvailable();
		void checkDatabaseState();

		void deleteDatabaseIfDebug();
		bool replace(std::string& str, const std::string& from, const std::string& to);

		const std::string PROJECT_COLLECTION_IDENTIFIER = "projects";
		const std::string DATA_ELEMENTS_COLLECTION_IDENTIFIER = "data_elements";
		const std::string COMMIT_COLLECTION_IDENTIFIER = "commits";
		const std::string DATA_VERSION_COLLECTION_IDENTIFIER = "data_versions";
		const std::string TAG_COLLECTION_IDENTIFIER = "tags";
		const std::string BRANCHES_COLLECTION_IDENTIFIER = "branches";
		const std::string USER_COLLECTION_IDENTIFIER = "users";
		const std::string DIGITAL_TWIN_COLLECTION_IDENTIFIER = "digital_twins";
		const std::string VERSION_COLLECTION_IDENTIFIER = "versions";
	};
}

