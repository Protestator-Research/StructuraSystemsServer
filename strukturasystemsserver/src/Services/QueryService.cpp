#include "QueryService.h"

#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/Constraint.h>
#include <sysmlv2/rest/entities/Data.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Query.h>
#include <sysmlv2/rest/entities/QueryRequest.h>
#include <sysmlv2/rest/versioning/QueryEvaluation.h>

#include <algorithm>
#include <mutex>
#include <shared_mutex>

#include "../Controller/DataBaseController.h"
#include "ProjectVersioningService.h"

namespace StructuraSystems::Server
{
	namespace
	{
		std::vector<boost::uuids::uuid> scopeIds(const std::vector<std::shared_ptr<SysMLv2::REST::Data>>& scope)
		{
			std::vector<boost::uuids::uuid> ids;
			for (const auto& data : scope)
			{
				if (data != nullptr)
					ids.push_back(data->getId());
			}
			return ids;
		}
	}

	std::shared_ptr<QueryService> QueryService::getInstance()
	{
		struct concreteQueryService : public QueryService {};
		// A function-local static is initialised exactly once, even with concurrent first calls.
		static const std::shared_ptr<QueryService> instance = std::make_shared<concreteQueryService>();
		return instance;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Query>> QueryService::getQueries(const std::shared_ptr<SysMLv2::REST::Project>& project) const
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdQueryMap.find(project->getId());
		if (it == ProjectIdQueryMap.end())
			return {};
		return it->second;
	}

	std::shared_ptr<SysMLv2::REST::Query> QueryService::getQueryById(const std::shared_ptr<SysMLv2::REST::Project>& project,
		const boost::uuids::uuid& queryId) const
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdQueryMap.find(project->getId());
		if (it == ProjectIdQueryMap.end())
			return nullptr;

		for (const auto& query : it->second)
		{
			if (query->getId() == queryId)
				return query;
		}
		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Query> QueryService::createQuery(const std::shared_ptr<SysMLv2::REST::Project>& project,
		SysMLv2::REST::QueryRequest& request)
	{
		const auto query = std::make_shared<SysMLv2::REST::Query>(request.getName(), request.getSelect(), request.getScope(),
			request.getOrderBy(), request.getWhere(), project);

		std::unique_lock lock(Mutex);
		// Persist first: if the database refuses the query, the service is unchanged.
		DBController->addQuery(project->getId(), query);
		ProjectIdQueryMap[project->getId()].push_back(query);
		return query;
	}

	std::shared_ptr<SysMLv2::REST::Query> QueryService::updateQuery(const std::shared_ptr<SysMLv2::REST::Project>& project,
		const boost::uuids::uuid& queryId, SysMLv2::REST::QueryRequest& request)
	{
		std::unique_lock lock(Mutex);
		const auto it = ProjectIdQueryMap.find(project->getId());
		if (it == ProjectIdQueryMap.end())
			return nullptr;

		for (const auto& query : it->second)
		{
			if (query->getId() != queryId)
				continue;

			// The query is changed before it is stored, so the old state is kept to be put back if the database refuses.
			const auto oldName = query->getName();
			const auto oldSelect = query->getSelect();
			const auto oldScope = query->getScope();
			const auto oldOrderBy = query->getOrderBy();
			const auto oldWhere = query->getWhere();

			query->setName(request.getName());
			query->setSelect(request.getSelect());
			query->setScope(request.getScope());
			query->setOrderBy(request.getOrderBy());
			query->setWhere(request.getWhere());
			try {
				DBController->updateQuery(project->getId(), query);
			}
			catch (...) {
				query->setName(oldName);
				query->setSelect(oldSelect);
				query->setScope(oldScope);
				query->setOrderBy(oldOrderBy);
				query->setWhere(oldWhere);
				throw;
			}
			return query;
		}
		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Query> QueryService::deleteQuery(const std::shared_ptr<SysMLv2::REST::Project>& project,
		const boost::uuids::uuid& queryId)
	{
		std::unique_lock lock(Mutex);
		const auto it = ProjectIdQueryMap.find(project->getId());
		if (it == ProjectIdQueryMap.end())
			return nullptr;

		auto& queries = it->second;
		const auto found = std::find_if(queries.begin(), queries.end(),
			[&queryId](const std::shared_ptr<SysMLv2::REST::Query>& candidate) { return candidate->getId() == queryId; });
		if (found == queries.end())
			return nullptr;

		const auto query = *found;
		DBController->deleteQuery(query);
		queries.erase(found);
		return query;
	}

	void QueryService::deleteQueriesOfProject(const boost::uuids::uuid& projectId)
	{
		std::unique_lock lock(Mutex);
		DBController->deleteQueriesOfProject(projectId);
		ProjectIdQueryMap.erase(projectId);
	}

	std::string QueryService::serializeQuery(const std::shared_ptr<SysMLv2::REST::Query>& query) const
	{
		std::shared_lock lock(Mutex);
		return query->serializeToJson();
	}

	std::shared_ptr<SysMLv2::REST::Commit> QueryService::commitToRunOn(const std::shared_ptr<SysMLv2::REST::Project>& project,
		const std::shared_ptr<SysMLv2::REST::Commit>& commit) const
	{
		if (commit != nullptr)
			return commit;

		const auto versioning = ProjectVersioningService::getInstance();
		const auto defaultBranch = versioning->getDefaultBranch(project);
		return defaultBranch != nullptr ? versioning->getHeadCommit(project, defaultBranch) : nullptr;
	}

	std::vector<nlohmann::json> QueryService::executeQueryById(const std::shared_ptr<SysMLv2::REST::Project>& project,
		const boost::uuids::uuid& queryId, const std::shared_ptr<SysMLv2::REST::Commit>& commit)
	{
		// A snapshot of the query, so the evaluation does not run under the lock.
		std::vector<std::string> select;
		std::vector<std::string> orderBy;
		std::vector<boost::uuids::uuid> scope;
		std::shared_ptr<SysMLv2::REST::Constraint> where;
		{
			std::shared_lock lock(Mutex);
			const auto it = ProjectIdQueryMap.find(project->getId());
			if (it == ProjectIdQueryMap.end())
				return {};
			const auto found = std::find_if(it->second.begin(), it->second.end(),
				[&queryId](const std::shared_ptr<SysMLv2::REST::Query>& candidate) { return candidate->getId() == queryId; });
			if (found == it->second.end())
				return {};
			select = (*found)->getSelect();
			orderBy = (*found)->getOrderBy();
			scope = scopeIds((*found)->getScope());
			where = (*found)->getWhere();
		}

		const auto runOn = commitToRunOn(project, commit);
		if (runOn == nullptr)
			return {};
		const auto state = ProjectVersioningService::getInstance()->getVersionedData(project, runOn);
		return SysMLv2::REST::Versioning::executeQuery(select, scope, orderBy, where, *state);
	}

	std::vector<nlohmann::json> QueryService::executeQuery(const std::shared_ptr<SysMLv2::REST::Project>& project,
		SysMLv2::REST::QueryRequest& request, const std::shared_ptr<SysMLv2::REST::Commit>& commit)
	{
		const auto runOn = commitToRunOn(project, commit);
		if (runOn == nullptr)
			return {};
		const auto state = ProjectVersioningService::getInstance()->getVersionedData(project, runOn);
		return SysMLv2::REST::Versioning::executeQuery(request, *state);
	}

	QueryService::QueryService()
	{
		DBController = DataBaseController::getInstance();
		ProjectIdQueryMap = DBController->getAllQueries();
	}
}
