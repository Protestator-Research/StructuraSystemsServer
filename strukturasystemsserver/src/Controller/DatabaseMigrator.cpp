#include "DatabaseMigrator.h"

#include <bsoncxx/builder/basic/array.hpp>
#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/basic/kvp.hpp>
#include <bsoncxx/types.hpp>
#include <mongocxx/options/update.hpp>
#include <sodium.h>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace StructuraSystems::Server
{
	namespace
	{
		using bsoncxx::builder::basic::kvp;
		using bsoncxx::builder::basic::make_array;
		using bsoncxx::builder::basic::make_document;

		std::int64_t modifiedCount(const bsoncxx::v_noabi::stdx::optional<mongocxx::result::update>& result)
		{
			return result ? result->modified_count() : 0;
		}
	}

	DatabaseMigrator::DatabaseMigrator(mongocxx::database database)
		: Database(std::move(database))
	{
	}

	void DatabaseMigrator::run(bool freshlyInitialized)
	{
		try
		{
			if (freshlyInitialized)
			{
				writeSchemaVersion(CURRENT_SCHEMA_VERSION);
				std::cout << "[migration] New database initialised, schema version set to " << CURRENT_SCHEMA_VERSION << std::endl;
				return;
			}

			const auto version = readSchemaVersion();
			if (version > CURRENT_SCHEMA_VERSION)
			{
				std::cerr << "[migration] ERROR: Database schema version " << version
					<< " is newer than the version supported by this binary (" << CURRENT_SCHEMA_VERSION
					<< "). Refusing to start." << std::endl;
				throw std::runtime_error("Database schema is newer than this server binary");
			}

			if (version == CURRENT_SCHEMA_VERSION)
			{
				std::cout << "[migration] Database schema is up to date (version " << version << ")" << std::endl;
				return;
			}

			std::cout << "[migration] Database schema version " << version << ", migrating to " << CURRENT_SCHEMA_VERSION << std::endl;
			if (version <= 1)
				migrateFrom1To2();

			writeSchemaVersion(CURRENT_SCHEMA_VERSION);
			std::cout << "[migration] Done, schema version is now " << CURRENT_SCHEMA_VERSION << std::endl;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[migration] FAILED: " << e.what() << std::endl;
			throw;
		}
	}

	std::int32_t DatabaseMigrator::readSchemaVersion()
	{
		const auto doc = Database["schema"].find_one(make_document(kvp("_id", "schema")));
		// A missing document means the database was created before schema versioning existed.
		if (!doc)
			return 1;

		const auto element = doc->view()["version"];
		if (element.type() == bsoncxx::type::k_int32)
			return element.get_int32().value;
		if (element.type() == bsoncxx::type::k_int64)
			return static_cast<std::int32_t>(element.get_int64().value);

		throw std::runtime_error("Schema document has no valid integer 'version'");
	}

	void DatabaseMigrator::writeSchemaVersion(std::int32_t version)
	{
		mongocxx::options::update options;
		options.upsert(true);
		Database["schema"].update_one(
			make_document(kvp("_id", "schema")),
			make_document(kvp("$set", make_document(kvp("version", version)))),
			options);
	}

	void DatabaseMigrator::migrateFrom1To2()
	{
		std::cout << "[migration] 1 -> 2: start" << std::endl;

		auto projects = Database["projects"];
		auto users = Database["users"];

		// Matches documents where the field is missing, null or the empty string.
		const auto emptyValues = [] { return make_array(bsoncxx::types::b_null{}, ""); };

		// 1. Preloaded projects become library projects.
		{
			const auto result = projects.update_many(
				make_document(
					kvp("description", "Preloaded Project"),
					kvp("owner", make_document(kvp("$in", emptyValues()))),
					kvp("owningGroup", make_document(kvp("$in", emptyValues())))),
				make_document(kvp("$set", make_document(kvp("isLibrary", true)))));
			std::cout << "[migration] 1 -> 2: step 1 library projects flagged: " << modifiedCount(result) << std::endl;
		}

		// 2. Report remaining projects without an owner.
		{
			auto cursor = projects.find(make_document(
				kvp("owner", make_document(kvp("$in", emptyValues()))),
				kvp("isLibrary", make_document(kvp("$ne", true)))));
			int count = 0;
			for (auto&& doc : cursor)
			{
				const auto id = doc["_id"];
				const auto name = doc["name"];
				std::cout << "[migration] WARNING: project without owner will be admin-only, id="
					<< (id && id.type() == bsoncxx::type::k_string ? std::string(id.get_string().value) : std::string("?"))
					<< " name=" << (name && name.type() == bsoncxx::type::k_string ? std::string(name.get_string().value) : std::string("?"))
					<< std::endl;
				++count;
			}
			std::cout << "[migration] 1 -> 2: step 2 projects without owner: " << count << std::endl;
		}

		// 3. Users: role, group, mustChangePassword.
		{
			const auto roleResult = users.update_many(
				make_document(kvp("$or", make_array(
					make_document(kvp("role", make_document(kvp("$exists", false)))),
					make_document(kvp("role", make_document(kvp("$nin", make_array(0, 1, 2, 100)))))))),
				make_document(kvp("$set", make_document(kvp("role", 0)))));
			const auto groupResult = users.update_many(
				make_document(kvp("group", bsoncxx::types::b_null{})),
				make_document(kvp("$set", make_document(kvp("group", "")))));
			const auto flagResult = users.update_many(
				make_document(kvp("mustChangePassword", make_document(kvp("$exists", false)))),
				make_document(kvp("$set", make_document(kvp("mustChangePassword", false)))));
			std::cout << "[migration] 1 -> 2: step 3 users role fixed: " << modifiedCount(roleResult)
				<< ", group set: " << modifiedCount(groupResult)
				<< ", mustChangePassword set: " << modifiedCount(flagResult) << std::endl;
		}

		// 4. Accounts that still use the default password "admin" must change it.
		{
			if (sodium_init() < 0)
				throw std::runtime_error("Failed to initialize libsodium");

			const std::string defaultPassword = "admin";
			int flagged = 0;
			for (auto&& doc : users.find({}))
			{
				const auto hash = doc["securityString"];
				const auto id = doc["_id"];
				if (!hash || hash.type() != bsoncxx::type::k_string || !id)
					continue;

				const std::string hashString(hash.get_string().value);
				if (crypto_pwhash_str_verify(hashString.c_str(), defaultPassword.c_str(), defaultPassword.size()) != 0)
					continue;

				const auto result = users.update_one(
					make_document(kvp("_id", id.get_value())),
					make_document(kvp("$set", make_document(kvp("mustChangePassword", true)))));
				std::cout << "[migration] WARNING: user '"
					<< (id.type() == bsoncxx::type::k_string ? std::string(id.get_string().value) : std::string("?"))
					<< "' still uses the default password and must change it (modified: " << modifiedCount(result) << ")" << std::endl;
				++flagged;
			}
			std::cout << "[migration] 1 -> 2: step 4 default-password accounts: " << flagged << std::endl;
		}

		std::cout << "[migration] 1 -> 2: end" << std::endl;
	}
}
