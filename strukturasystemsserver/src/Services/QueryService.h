#pragma once

#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>
#include <boost/uuid/uuid.hpp>
#include <nlohmann/json.hpp>

namespace SysMLv2::REST
{
	class Commit;
	class Project;
	class Query;
	class QueryRequest;
}

namespace StructuraSystems::Server
{
	class DataBaseController;

	/// The queries of the projects (QueryService, 7.2.4) and their execution on the data of a commit.
	/// Lock order: ServerProjectService -> QueryService. The execution takes the lock of ProjectVersioningService only after
	/// it has left its own, so the two are never held together.
	class QueryService
	{
	public:
		static std::shared_ptr<QueryService> getInstance();

		std::vector<std::shared_ptr<SysMLv2::REST::Query>> getQueries(const std::shared_ptr<SysMLv2::REST::Project>& project) const;

		/// nullptr if the project has no query with this id.
		std::shared_ptr<SysMLv2::REST::Query> getQueryById(const std::shared_ptr<SysMLv2::REST::Project>& project, const boost::uuids::uuid& queryId) const;

		std::shared_ptr<SysMLv2::REST::Query> createQuery(const std::shared_ptr<SysMLv2::REST::Project>& project, SysMLv2::REST::QueryRequest& request);

		/// Replaces name, select, scope, where and orderBy of the query. nullptr if the project has no query with this id.
		std::shared_ptr<SysMLv2::REST::Query> updateQuery(const std::shared_ptr<SysMLv2::REST::Project>& project,
			const boost::uuids::uuid& queryId, SysMLv2::REST::QueryRequest& request);

		/// nullptr if the project has no query with this id.
		std::shared_ptr<SysMLv2::REST::Query> deleteQuery(const std::shared_ptr<SysMLv2::REST::Project>& project, const boost::uuids::uuid& queryId);

		/// Deletes all queries of a deleted project.
		void deleteQueriesOfProject(const boost::uuids::uuid& projectId);

		/// Serializes the query under the shared lock, so it cannot race with updateQuery.
		std::string serializeQuery(const std::shared_ptr<SysMLv2::REST::Query>& query) const;

		/// The data of the commit that satisfy the stored query, one JSON object per Data object. Without a commit the head of the
		/// default branch is used; without a head the result is empty. nullptr for the query: the result is empty as well.
		std::vector<nlohmann::json> executeQueryById(const std::shared_ptr<SysMLv2::REST::Project>& project,
			const boost::uuids::uuid& queryId, const std::shared_ptr<SysMLv2::REST::Commit>& commit);

		/// The same for a query that is not stored.
		std::vector<nlohmann::json> executeQuery(const std::shared_ptr<SysMLv2::REST::Project>& project,
			SysMLv2::REST::QueryRequest& request, const std::shared_ptr<SysMLv2::REST::Commit>& commit);

	private:
		QueryService();

		/// The commit to run on: the given one, else the head of the default branch. nullptr if there is none.
		std::shared_ptr<SysMLv2::REST::Commit> commitToRunOn(const std::shared_ptr<SysMLv2::REST::Project>& project,
			const std::shared_ptr<SysMLv2::REST::Commit>& commit) const;

		/// Guards the map and the Query objects stored in it. Not recursive.
		mutable std::shared_mutex Mutex;

		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Query>>> ProjectIdQueryMap;

		DataBaseController* DBController;
	};
}
