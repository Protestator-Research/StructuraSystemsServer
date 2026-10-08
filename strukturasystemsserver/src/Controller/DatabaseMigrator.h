#pragma once

#include <cstdint>
#include <mongocxx/database.hpp>

namespace StructuraSystems::Server
{
	/// Versioned schema migration of the MongoDB database.
	/// The schema version is stored in the collection "schema" as {_id: "schema", version: N}.
	class DatabaseMigrator
	{
	public:
		static constexpr std::int32_t CURRENT_SCHEMA_VERSION = 2;

		explicit DatabaseMigrator(mongocxx::database database);

		/// Brings the database to CURRENT_SCHEMA_VERSION.
		/// A freshly initialised database is stamped with the current version without running migrations.
		/// Throws if the migration fails or if the database is newer than this binary.
		void run(bool freshlyInitialized);

	private:
		std::int32_t readSchemaVersion();
		void writeSchemaVersion(std::int32_t version);
		void migrateFrom1To2();

		mongocxx::database Database;
	};
}
