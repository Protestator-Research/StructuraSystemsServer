#include "DataBaseController.h"
#include "../Entities/json/TwinResponse.h"
#include "VersionController.h"
#include "../Entities/db/Version.hpp"

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <nlohmann/json.hpp>
#include <vector>
#include <memory>
#include <stdexcept>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <kerml/root/annotations/TextualRepresentation.h>
#include <nlohmann/json_fwd.hpp>
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/JSONEntities.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/serialization/Utilities.h>
#include <sysmlv2/service/implementation/ElementNavigationService.h>
#include <sysmlv2/rest/serialization/SysMLv2Deserializer.h>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <sysmlv2/rest/entities/Tag.h>




namespace StructuraSystems::Server
{
	DataBaseController* DataBaseController::Instance = nullptr;

	DataBaseController* DataBaseController::getInstance()
	{
		if (!Instance)
			throw std::runtime_error("No Instance available");

		return Instance;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Project>> DataBaseController::getAllProjects()
	{
		auto collection = database["projects"];
		auto cursor = collection.find({});

		std::vector<std::shared_ptr<SysMLv2::REST::Project>> returnValue;
		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");
			const auto project = std::make_shared<SysMLv2::REST::Project>(dbString);
			returnValue.push_back(project);
		}
		std::cout << returnValue.size() << " Projects loaded from Database." << std::endl;
		return returnValue;
	}

	void DataBaseController::addMultibleProjects(std::vector<std::shared_ptr<SysMLv2::REST::Project>> projects)
	{
		std::vector<bsoncxx::document::value> dbProjects;
		for (const auto& project : projects)
		{
			std::string jsonString = project->serializeToJson();
			replace(jsonString, "@id", "_id");
			nlohmann::json json = nlohmann::json::parse(jsonString);
			dbProjects.push_back(bsoncxx::from_json(json.dump()));
		}
		database["projects"].insert_many(dbProjects);
	}

	void DataBaseController::addProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		std::string jsonString = project->serializeToJson();
		replace(jsonString, "@id", "_id");
		nlohmann::json json = nlohmann::json::parse(jsonString);
		database["projects"].insert_one(bsoncxx::from_json(json.dump()));
	}

	void DataBaseController::updateProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		std::string jsonString = project->serializeToJson();
		replace(jsonString, "@id", "_id");
		nlohmann::json json = nlohmann::json::parse(jsonString);
		auto query_filter = bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(project->getId())));
		auto update_project = bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("$set",bsoncxx::from_json(json.dump())));

		auto result = database["projects"].update_one(query_filter.view(), update_project.view());
	}

	bool DataBaseController::deleteProject(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		auto result = database["projects"].delete_one(bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(project->getId())), bsoncxx::builder::basic::kvp("name", project->getName()), bsoncxx::builder::basic::kvp("description", project->getDescription()), bsoncxx::builder::basic::kvp("defaultBranch", "{\"@id\":" + boost::uuids::to_string(project->getDefaultBranch()->getId()) + "}")));
		return (result.has_value() && (result.value().deleted_count() > 0));
	}

	void DataBaseController::addMultibleBranches(std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>> projectBranchMap)
	{
		std::vector<bsoncxx::document::value> dbBranches;
		for (const auto& [projectId , branches] : projectBranchMap)
		{
			for (const auto& branch : branches) {
				std::string jsonString = branch->serializeToJson();
				replace(jsonString, "@id", "_id");
				nlohmann::json json = nlohmann::json::parse(jsonString);
				json["_project_id"] = boost::uuids::to_string(projectId);
				dbBranches.push_back(bsoncxx::from_json(json.dump()));
			}
		}
		database["branches"].insert_many(dbBranches);
	}

	void DataBaseController::addBranch(boost::uuids::uuid projectId, std::shared_ptr<SysMLv2::REST::Branch> branch)
	{
		std::string jsonString = branch->serializeToJson();
		replace(jsonString, "@id", "_id");
		nlohmann::json json = nlohmann::json::parse(jsonString);
		json["_project_id"] = boost::uuids::to_string(projectId);
		database["branches"].insert_one(bsoncxx::from_json(json.dump()));
	}

	void DataBaseController::updateBranch(std::shared_ptr<SysMLv2::REST::Branch> branch) {
		auto collection = database[BRANCHES_COLLECTION_IDENTIFIER];
		auto filter = bsoncxx::builder::stream::document{} << "_id" << boost::uuids::to_string(branch->getId()) << bsoncxx::builder::stream::finalize;

		auto branchString = branch->serializeToJson();
		replace(branchString, "@id", "_id");

		auto update = bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("$set",bsoncxx::from_json(branchString)));

		auto result = collection.update_one(filter.view(), update.view());

		if (result && result->modified_count() == 1)
			std::cout << "Dokument aktualisiert\n";
		else
			std::cout << "Nichts geändert\n";
	}

	std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>> DataBaseController::getAllBranches()
	{
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>> returnValue;

		auto collection = database["branches"];
		auto cursor = collection.find({});

		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");
			nlohmann::json json = nlohmann::json::parse(dbString);
			auto projectID = boost::uuids::string_generator()(json["_project_id"].get<std::string>());
			json.erase("_project_id");
			const auto branch = std::make_shared<SysMLv2::REST::Branch>(json.dump());

			if (!returnValue.contains(projectID))
				returnValue.insert(std::make_pair(projectID, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>()));

			returnValue.at(projectID).push_back(branch);
		}
		std::cout << returnValue.size() << " Branches loaded from Database." << std::endl;
		return returnValue;
	}

	void DataBaseController::deleteBranch(std::shared_ptr<SysMLv2::REST::Branch> branch) {
		try {
			auto collection = database["branches"];

			auto filter = bsoncxx::builder::stream::document{}
				<< "_id" << boost::uuids::to_string(branch->getId())
				<< bsoncxx::builder::stream::finalize;

			const auto cursor = collection.delete_one(filter.view());
			if (cursor->deleted_count()==1)
				std::cout << "User "<< branch->getName() << " deleted" << std::endl;
		}
		catch (const std::exception& e) {
			std::cerr << "Ungültige ObjectId: " << e.what() << "\n";
		}
	}

	void DataBaseController::addMultibleCommits(std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> projectCommitMap)
	{
		std::vector<bsoncxx::document::value> dbCommits;
		for (const auto& [proj_id, commits] : projectCommitMap)
		{
			for (const auto& commit : commits) {

				std::string commitJsonString = commit->serializeToJson();
				replace(commitJsonString, "@id", "_id");

				dbCommits.push_back(bsoncxx::from_json(commitJsonString));

			}
		}
		database["commits"].insert_many(dbCommits);
	}

	void DataBaseController::addCommit(std::shared_ptr<SysMLv2::REST::Commit> commit)
	{
		std::string commitJsonString = commit->serializeToJson();
		replace(commitJsonString, "@id", "_id");

		database["commits"].insert_one(bsoncxx::from_json(commitJsonString));
	}

	std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> DataBaseController::getAllCommits()
	{
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> returnValue;

		auto collection = database["commits"];
		auto cursor = collection.find({});

		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");

			nlohmann::json json = nlohmann::json::parse(dbString);
			const auto commit = std::make_shared<SysMLv2::REST::Commit>(json.dump());

			if (!returnValue.contains(commit->getOwningProject()->getId()))
				returnValue.insert(std::make_pair(commit->getOwningProject()->getId(), std::vector<std::shared_ptr<SysMLv2::REST::Commit>>()));

			returnValue.at(commit->getOwningProject()->getId()).push_back(commit);
		}
		std::cout << returnValue.size() << " Commits loaded from Database." << std::endl;
		return returnValue;
	}

	void DataBaseController::addMultibleDataVersions(std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::DataVersion>> commitIdDataVersions)
	{
		std::vector<bsoncxx::document::value> dbDataVersions;
		for (const auto& [commitId, dataVersion] : commitIdDataVersions)
		{
			dbDataVersions.push_back(
				bsoncxx::builder::basic::make_document(
					bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(dataVersion->getId())),
					bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_PAYLOAD_ENTITY, dataVersion->getPayload()->serializeToJson()),
					bsoncxx::builder::basic::kvp("_id_commit", boost::uuids::to_string(commitId))
				));
		}
		database["data_versions"].insert_many(dbDataVersions);
	}

	void DataBaseController::addDataVersion(boost::uuids::uuid commitId, std::shared_ptr<SysMLv2::REST::DataVersion> dataVersion)
	{
		database["data_versions"].insert_one(bsoncxx::builder::basic::make_document(
			bsoncxx::builder::basic::kvp("_id", boost::uuids::to_string(dataVersion->getId())),
			bsoncxx::builder::basic::kvp(SysMLv2::REST::JSON_PAYLOAD_ENTITY, dataVersion->getPayload()->serializeToJson()),
			bsoncxx::builder::basic::kvp("_id_commit", boost::uuids::to_string(commitId))
		));
	}

	std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::DataVersion>> DataBaseController::getAllDataVersions()
	{
		auto collection = database["data_versions"];
		auto cursor = collection.find({});
		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::DataVersion>> returnValue;

		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");
			nlohmann::json json = nlohmann::json::parse(dbString);
			const auto& commitId = boost::uuids::string_generator()(json["_id_commit"].get<std::string>());
			json.erase("_id_commit");
			const auto& dataVersion = std::make_shared<SysMLv2::REST::DataVersion>(json.dump());
			returnValue.insert(std::make_pair(commitId, dataVersion));
		}

		return returnValue;
	}

	void DataBaseController::addMultibleElements(std::map<boost::uuids::uuid, std::shared_ptr<KerML::Entities::Element>> projectIDElementData)
	{
		std::vector<bsoncxx::document::value> dbElements;
		for (const auto& [projectId, dataElement]: projectIDElementData)
		{
			std::string jsonString = dataElement->serializeToJson();
			replace(jsonString, "@id", "_id");
			nlohmann::json json = nlohmann::json::parse(jsonString);
			json["_commit_id"] = boost::uuids::to_string(projectId);
			dbElements.push_back(bsoncxx::from_json(json.dump()));
		}

		database["data_elements"].insert_many(dbElements);
	}

	void DataBaseController::addElement(boost::uuids::uuid projectId, std::shared_ptr<KerML::Entities::Element> elementData)
	{
		std::string jsonString = elementData->serializeToJson();
		replace(jsonString, "@id", "_id");
		nlohmann::json json = nlohmann::json::parse(jsonString);
		json["_commit_id"] = boost::uuids::to_string(projectId);
		database["data_elements"].insert_one(bsoncxx::from_json(json.dump()));
	}

	std::map<boost::uuids::uuid, std::vector<std::shared_ptr<KerML::Entities::Element>>>DataBaseController::getAllElements()
	{
		auto collection = database["data_elements"];
		auto cursor = collection.find({});
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<KerML::Entities::Element>>> elementProijectIDMap;
		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");
			nlohmann::json json = nlohmann::json::parse(dbString);
			const auto& commitId = boost::uuids::string_generator()(json["_commit_id"].get<std::string>());
			json.erase("_commit_id");

			if (elementProijectIDMap.find(commitId) == elementProijectIDMap.end())
				elementProijectIDMap.insert(std::make_pair(commitId, std::vector<std::shared_ptr<KerML::Entities::Element>>()));

			const auto& element = std::dynamic_pointer_cast<KerML::Entities::Element>(SysMLv2::SysMLv2Deserializer::deserializeJsonString(json.dump()));
			elementProijectIDMap[commitId].push_back(element);
		}
		return elementProijectIDMap;
	}

	void DataBaseController::addTag(boost::uuids::uuid , std::shared_ptr<SysMLv2::REST::Tag> tag) {
		std::string jsonString = tag->serializeToJson();
		replace(jsonString, "@id", "_id");
		nlohmann::json json = nlohmann::json::parse(jsonString);
		database["tags"].insert_one(bsoncxx::from_json(json.dump()));
	}

	void DataBaseController::deleteTag(std::shared_ptr<SysMLv2::REST::Tag> tag) {
		try {
			auto collection = database["tags"];

			auto filter = bsoncxx::builder::stream::document{}
			<< "_id" << boost::uuids::to_string(tag->getId())
			<< bsoncxx::builder::stream::finalize;

			const auto cursor = collection.delete_one(filter.view());
			if (cursor->deleted_count()==1)
				std::cout << "Tag "<< tag->getName() << " deleted" << std::endl;
		}
		catch (const std::exception& e) {
			std::cerr << "Ungültige ObjectId: " << e.what() << "\n";
		}
	}

	std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Tag>>> DataBaseController::getAllTags() {
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Tag>>> returnValue;

		auto collection = database["tags"];
		auto cursor = collection.find({});
		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");

			auto tag = std::make_shared<SysMLv2::REST::Tag>(dbString);

			if (returnValue.find(tag->owningProject()->getId()) == returnValue.end())
				returnValue.insert(std::make_pair(tag->owningProject()->getId(), std::vector<std::shared_ptr<SysMLv2::REST::Tag>>()));

			returnValue[tag->owningProject()->getId()].push_back(tag);
		}
		return returnValue;
	}

	void DataBaseController::addTwin(boost::uuids::uuid, std::shared_ptr<StructuraSystems::Server::TwinResponse> twinTag) {
		std::string jsonString = twinTag->serializeToJson();
		replace(jsonString, "@id", "_id");
		nlohmann::json json = nlohmann::json::parse(jsonString);
		database["digital_twins"].insert_one(bsoncxx::from_json(json.dump()));
	}

	void DataBaseController::deleteTwin(std::shared_ptr<StructuraSystems::Server::TwinResponse> twinTag) {
		try {
			auto collection = database["digital_twins"];

			auto filter = bsoncxx::builder::stream::document{}
			<< "_id" << boost::uuids::to_string(twinTag->getId())
			<< bsoncxx::builder::stream::finalize;

			const auto cursor = collection.delete_one(filter.view());
			if (cursor->deleted_count()==1)
				std::cout << "Tag "<< twinTag->getName() << " deleted" << std::endl;
		}
		catch (const std::exception& e) {
			std::cerr << "Ungültige ObjectId: " << e.what() << "\n";
		}
	}

	std::map<boost::uuids::uuid, std::vector<std::shared_ptr<StructuraSystems::Server::TwinResponse>>> DataBaseController::getAllTwins() {
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<TwinResponse>>> returnValue;

		auto collection = database["digital_twins"];
		auto cursor = collection.find({});
		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			replace(dbString, "_id", "@id");

			auto tag = std::make_shared<TwinResponse>(dbString);

			if (returnValue.find(tag->owningProject()->getId()) == returnValue.end())
				returnValue.insert(std::make_pair(tag->owningProject()->getId(), std::vector<std::shared_ptr<TwinResponse>>()));

			returnValue[tag->owningProject()->getId()].push_back(tag);
		}
		return returnValue;
	}

	void DataBaseController::addUser(const User& user)
	{
		database["users"].insert_one(bsoncxx::from_json(user.getJson()));
	}

	std::map<std::string, User> DataBaseController::getAllUser()
	{
		auto collection = database["users"];
		auto cursor = collection.find({});
		std::map<std::string, User> returnValue;
		for (auto&& doc : cursor)
		{
			std::string dbString = bsoncxx::to_json(doc);
			auto user = User(dbString);
			returnValue.insert(std::make_pair(
				user.username(),
				std::move(user)
			));
		}
		return returnValue;
	}

	void DataBaseController::removeUser(std::string username) {
		try {
			auto collection = database["users"];
			auto filter = bsoncxx::builder::stream::document{} << "_id" << username << bsoncxx::builder::stream::finalize;
			auto cursor = collection.delete_one(filter.view());
			if (cursor->deleted_count()==1)
				std::cout << "User "<< username << " deleted" << std::endl;
		}
		catch (const std::exception& e) {
			std::cerr << "Ungültige ObjectId: " << e.what() << "\n";
		}
	}

	void DataBaseController::changeUser(const User &user) {
		auto collection = database["users"];
		auto filter = bsoncxx::builder::stream::document{} << "_id" << user.username() << bsoncxx::builder::stream::finalize;

		auto update = bsoncxx::builder::stream::document{}
		<< "$set"
			<< bsoncxx::builder::stream::open_document
				<< "securityString" << user.hashedPassword()
				<< "group" << user.group()
				<< "role" << (int)user.role()
			<< bsoncxx::builder::stream::close_document
		<< bsoncxx::builder::stream::finalize;

		auto result = collection.update_one(filter.view(), update.view());

		if (result && result->modified_count() == 1)
			std::cout << "Dokument aktualisiert\n";
		else
			std::cout << "Nichts geändert\n";
	}


	DataBaseController* DataBaseController::createInstance(std::string dbAddress, std::string username, std::string password)
	{
		if (Instance)
			return Instance;

		Instance = new DataBaseController(dbAddress, username, password);
		return Instance;
	}

	DataBaseController::DataBaseController(std::string dBAddress, std::string username, std::string password)
	{
		std::string uri_string = "";

		if (username.empty())
			uri_string += "mongodb://" + dBAddress;
		else
			uri_string += "mongodb://" + username + ":" + password + "@" + dBAddress;

		uri = mongocxx::uri(uri_string);
		client = mongocxx::client(uri);

		database = client["structura_systems"];

// #ifndef NDEBUG
		// deleteDatabaseIfDebug();
// #endif
		checkDatabaseState();
	}

	void DataBaseController::addVersionToDatabase() {
		const auto version = VersionController::getInstance()->getVersionElement();
		database[VERSION_COLLECTION_IDENTIFIER].insert_one(bsoncxx::from_json(version.serialiseJson()));
	}

	void DataBaseController::initializeDatabaseIfNotAvailable()
	{
		database.create_collection(PROJECT_COLLECTION_IDENTIFIER);
		database.create_collection(DATA_ELEMENTS_COLLECTION_IDENTIFIER);
		database.create_collection(COMMIT_COLLECTION_IDENTIFIER);
		database.create_collection(DATA_VERSION_COLLECTION_IDENTIFIER);
		database.create_collection(TAG_COLLECTION_IDENTIFIER);
		database.create_collection(BRANCHES_COLLECTION_IDENTIFIER);
		database.create_collection(USER_COLLECTION_IDENTIFIER);
		database.create_collection(DIGITAL_TWIN_COLLECTION_IDENTIFIER);
		database.create_collection(VERSION_COLLECTION_IDENTIFIER);

		addVersionToDatabase();

		std::vector<std::shared_ptr<SysMLv2::REST::Project>> projects = {
			std::make_shared<SysMLv2::REST::Project>("AnalysisTooling.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SampledFunctions.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("StateSpaceRepresentation.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("TradeStudies.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("CausationConnections.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("CauseAndEffect.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ShapeItems.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SpatialItems.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ImageMetadata.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ModelingMetadata.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ParametersOfInterest.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("RiskMetadata.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQ.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQAcoustics.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQAtomicNuclear.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQBase.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQCharacteristicNumbers.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQChemistryMolecular.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQCondensedMatter.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQElectromagnetism.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQInformation.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQLight.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQMechanics.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQSpaceTime.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ISQThermodynamics.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("MeasurementRefCalculations.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("MeasurementReferences.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Quantities.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("QuantityCalculations.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SI.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SIPrefixes.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("TensorCalculations.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Time.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("USCustomaryUnits.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("VectorCalculations.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("DerivationConnections.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("RequirementDerivation.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Collections.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ScalarValues.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("VectorValues.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("BaseFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("BooleanFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("CollectionFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ComplexFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ControlFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("DataFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("IntegerFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("NaturalFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("NumericalFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("OccurrenceFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("RationalFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("RealFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ScalarFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SequenceFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("StringFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("TrigFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("VectorFunctions.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Base.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Clocks.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("ControlPerformances.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("FeatureReferencingPerformances.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("KerML.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Links.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Metaobjects.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Objects.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Observation.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Occurrences.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Performances.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SpatialFrames.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("StatePerformances.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Transfers.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("TransitionPerformances.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Triggers.kerml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Actions.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Allocations.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("AnalysisCases.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Attributes.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Calculations.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Cases.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Connections.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Constraints.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Flows.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Interfaces.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Items.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Metadata.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Parts.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Ports.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Requirements.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("StandardViewDefinitions.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("States.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("SysML.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("UseCases.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("VerificationCases.sysml", "Preloaded Project", "Main"),
			std::make_shared<SysMLv2::REST::Project>("Views.sysml", "Preloaded Project", "Main")
		};
		addMultibleProjects(projects);

		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>> projectBranchMap;
		for (const auto& project : projects)
		{
			projectBranchMap.insert(std::make_pair(project->getId(), std::vector<std::shared_ptr<SysMLv2::REST::Branch>>{project->getDefaultBranch()}));
		}

		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> commitProjectMap;
		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::DataVersion>> commitIdDataVersionMap;
		std::map<boost::uuids::uuid, std::shared_ptr<KerML::Entities::Element>> commitIdElementsMap;

		for (const auto& project : projects)
		{
			auto commit = std::make_shared<SysMLv2::REST::Commit>("Initial Commit from Structura Systems",project);

			std::ifstream file;
			file.open("resources/sysml/" + project->getName());
			std::string line;
			std::string content = "";
			while (std::getline(file, line))
			{
				content += line + "\n";
			}
			const auto& payload = std::make_shared<KerML::Entities::TextualRepresentation>("SysMLv2", content);
			auto change = std::make_shared<SysMLv2::REST::DataVersion>(boost::uuids::random_generator()(),payload);
			commit->addChange(change);
			commitProjectMap.insert(std::make_pair(project->getId(), std::vector<std::shared_ptr<SysMLv2::REST::Commit>>{ commit }));
			project->getDefaultBranch()->setHead(commit);
			project->getDefaultBranch()->setReferencedCommit(commit);
			commitIdElementsMap.insert(std::make_pair(commit->getId(), payload));
			commitIdDataVersionMap.insert(std::make_pair(commit->getId(), commit->getDataVersion()[0]));
		}

		addMultibleBranches(projectBranchMap);
		addMultibleCommits(commitProjectMap);
		addMultibleDataVersions(commitIdDataVersionMap);
		addMultibleElements(commitIdElementsMap);
	}

	void DataBaseController::checkDatabaseState() {
		try
		{
			const auto& collectionNames = database.list_collection_names();
			if (collectionNames.size()==0)
				initializeDatabaseIfNotAvailable();
			// This else clause only exists, because this project started as a sidequest in my dissertation.
			else if (std::find(collectionNames.begin(), collectionNames.end(), VERSION_COLLECTION_IDENTIFIER) == collectionNames.end()) {
				deleteDatabaseIfDebug();
				initializeDatabaseIfNotAvailable();
			}
			//All other database transformations need to be done after this statement.
		}
		catch (...) {
			initializeDatabaseIfNotAvailable();
		}
	}

	void DataBaseController::deleteDatabaseIfDebug()
	{
		std::cout << "\033[31m" << "----------------------------------------------------------------------" << std::endl;
		std::cout << "       Deleting Database." << std::endl;
		std::cout << "----------------------------------------------------------------------" << "\033[0m" << std::endl;

		try
		{
			database.drop();
		}
		catch (...)
		{
			std::cout << "Could not delete DB, since no DB avaiable" << std::endl;
		}
	}

	bool DataBaseController::replace(std::string& str, const std::string& from, const std::string& to)
	{
		size_t start_pos = str.find(from);

		if (start_pos == std::string::npos)
			return false;

		str.replace(start_pos, from.length(), to);

		return true;
	}
}
