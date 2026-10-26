#pragma once

#include <drogon/HttpController.h>

#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/Query.h>
#include <sysmlv2/rest/entities/QueryRequest.h>

#include <nlohmann/json.hpp>
#include <string>

#include "../Services/ProjectVersioningService.h"
#include "../Services/QueryService.h"
#include "../Filter/JwtFilter.hpp"
#include "Helper/RequestHelper.hpp"
#include "Helper/ProjectAccessHelper.hpp"

namespace StructuraSystems::Server
{
	/// The routes of the QueryService (Table 10 of the specification).
	class QueryController : public drogon::HttpController<StructuraSystems::Server::QueryController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(QueryController::getQueries, "/projects/{1:projectId}/queries", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(QueryController::postQuery, "/projects/{1:projectId}/queries", drogon::Post, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(QueryController::getQueryById, "/projects/{1:projectId}/queries/{2:queryId}", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(QueryController::putQuery, "/projects/{1:projectId}/queries/{2:queryId}", drogon::Put, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(QueryController::deleteQuery, "/projects/{1:projectId}/queries/{2:queryId}", drogon::Delete, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(QueryController::getQueryResults, "/projects/{1:projectId}/queries/{2:queryId}/results", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(QueryController::executeQuery, "/projects/{1:projectId}/query-results", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(QueryController::executeQuery, "/projects/{1:projectId}/query-results", drogon::Post, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END

		void getQueries(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			callback(paginate(request, Queries->getQueries(project),
				[](const std::shared_ptr<SysMLv2::REST::Query>& query) { return boost::uuids::to_string(query->getId()); },
				[this](const std::shared_ptr<SysMLv2::REST::Query>& query) { return Queries->serializeQuery(query); }));
		}

		void postQuery(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto queryRequest = parseQueryRequest(request, callback);
			if (queryRequest == nullptr)
				return;

			try {
				const auto query = Queries->createQuery(project, *queryRequest);
				respond(callback, drogon::k201Created, Queries->serializeQuery(query));
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
			}
		}

		void getQueryById(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& queryId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedQueryId = parseUuid(queryId);
			if (!parsedQueryId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid query id."));
				return;
			}

			const auto query = Queries->getQueryById(project, *parsedQueryId);
			if (query == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Query not found."));
				return;
			}
			respond(callback, drogon::k200OK, Queries->serializeQuery(query));
		}

		void putQuery(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& queryId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto parsedQueryId = parseUuid(queryId);
			if (!parsedQueryId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid query id."));
				return;
			}

			const auto queryRequest = parseQueryRequest(request, callback);
			if (queryRequest == nullptr)
				return;

			try {
				const auto query = Queries->updateQuery(project, *parsedQueryId, *queryRequest);
				if (query == nullptr)
				{
					callback(jsonError(drogon::k404NotFound, "Query not found."));
					return;
				}
				respond(callback, drogon::k200OK, Queries->serializeQuery(query));
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
			}
		}

		void deleteQuery(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& queryId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto parsedQueryId = parseUuid(queryId);
			if (!parsedQueryId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid query id."));
				return;
			}

			try {
				const auto query = Queries->deleteQuery(project, *parsedQueryId);
				if (query == nullptr)
				{
					callback(jsonError(drogon::k404NotFound, "Query not found."));
					return;
				}
				respond(callback, drogon::k200OK, query->serializeToJson());
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
			}
		}

		void getQueryResults(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& queryId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedQueryId = parseUuid(queryId);
			if (!parsedQueryId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid query id."));
				return;
			}
			if (Queries->getQueryById(project, *parsedQueryId) == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Query not found."));
				return;
			}

			std::shared_ptr<SysMLv2::REST::Commit> commit;
			if (!selectCommit(request, project, callback, commit))
				return;

			try {
				respondWithRows(request, callback, Queries->executeQueryById(project, *parsedQueryId, commit));
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
			}
		}

		/// GET and POST /projects/{id}/query-results: the query is in the body and is not stored.
		void executeQuery(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto queryRequest = parseQueryRequest(request, callback);
			if (queryRequest == nullptr)
				return;

			std::shared_ptr<SysMLv2::REST::Commit> commit;
			if (!selectCommit(request, project, callback, commit))
				return;

			try {
				respondWithRows(request, callback, Queries->executeQuery(project, *queryRequest, commit));
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
			}
		}

	private:
		static void respond(const std::function<void(const drogon::HttpResponsePtr&)>& callback, drogon::HttpStatusCode status, const std::string& body)
		{
			const auto response = drogon::HttpResponse::newHttpResponse(status, drogon::CT_APPLICATION_JSON);
			response->setBody(body);
			callback(response);
		}

		/// The rows of a result are identified by their @id (each Data object appears once, in the order of its identity).
		static void respondWithRows(const drogon::HttpRequestPtr& request, const std::function<void(const drogon::HttpResponsePtr&)>& callback,
			const std::vector<nlohmann::json>& rows)
		{
			callback(paginate(request, rows,
				[](const nlohmann::json& row) { return row.value("@id", std::string()); },
				[](const nlohmann::json& row) { return row.dump(2); }));
		}

		/// Reads a QueryRequest from the body. Answers 400 and returns nullptr if the body is no valid query.
		static std::shared_ptr<SysMLv2::REST::QueryRequest> parseQueryRequest(const drogon::HttpRequestPtr& request,
			const std::function<void(const drogon::HttpResponsePtr&)>& callback)
		{
			const auto jsonObject = requireJson(request);
			if (jsonObject == nullptr || !jsonObject->isObject())
			{
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return nullptr;
			}
			try {
				return std::make_shared<SysMLv2::REST::QueryRequest>(jsonObject->toStyledString());
			}
			catch (const std::exception& exception) {
				callback(jsonError(drogon::k400BadRequest, std::string("Invalid query: ") + exception.what()));
			}
			return nullptr;
		}

		/// The commit of the parameter commitId. Without the parameter the commit stays nullptr (the service then uses the head
		/// of the default branch). Answers 400/404 through the callback and returns false for an invalid or unknown commit.
		bool selectCommit(const drogon::HttpRequestPtr& request, const std::shared_ptr<Project>& project,
			const std::function<void(const drogon::HttpResponsePtr&)>& callback, std::shared_ptr<SysMLv2::REST::Commit>& commit)
		{
			const auto commitIds = queryValues(request, "commitId");
			if (commitIds.empty())
				return true;

			const auto parsed = parseUuid(commitIds.front());
			if (!parsed)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return false;
			}
			commit = ProjectVerService->getCommitById(project, *parsed);
			if (commit == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Commit not found."));
				return false;
			}
			return true;
		}

		std::shared_ptr<QueryService> Queries = QueryService::getInstance();
		std::shared_ptr<ProjectVersioningService> ProjectVerService = ProjectVersioningService::getInstance();
	};
}
