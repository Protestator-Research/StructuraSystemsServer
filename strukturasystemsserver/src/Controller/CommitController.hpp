#pragma once

#include <drogon/HttpController.h>

#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/BranchRequest.h>
#include <sysmlv2/rest/entities/Tag.h>
#include <sysmlv2/rest/entities/Data.h>
#include <sysmlv2/rest/entities/DataDifference.h>
#include <sysmlv2/rest/entities/ExternalData.h>
#include <sysmlv2/rest/entities/ExternalRelationship.h>
#include <sysmlv2/rest/entities/MergeResult.h>
#include <sysmlv2/rest/entities/ProjectUsage.h>
#include <sysmlv2/rest/serialization/SysMLv2Deserializer.h>
#include <kerml/root/elements/Element.h>

#include <string>
#include <typeinfo>

#include "../Services/ProjectVersioningService.h"
#include "../Services/ServerProjectService.h"
#include "../Filter/JwtFilter.hpp"
#include "Helper/RequestHelper.hpp"
#include "Helper/ProjectAccessHelper.hpp"

namespace StructuraSystems::Server
{
	class CommitController : public drogon::HttpController<StructuraSystems::Server::CommitController>
	{
	public:
		METHOD_LIST_BEGIN
		ADD_METHOD_TO(CommitController::getCommits, "/projects/{1:projectId}/commits", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::getCommitId,"/projects/{1:projectId}/commits/{2:commitId}", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::postCommit,"/projects/{1:projectId}/commits", drogon::Post, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::getCommitChange, "/projects/{1:projectId}/commits/{2:commitId}/changes", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::getCommitChangeById, "/projects/{1:projectId}/commits/{2:commitId}/changes/{3:changeId}", drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::getBranches,"/projects/{1:projectId}/branches",drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::postBranch,"/projects/{1:projectId}/branches",drogon::Post, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::getBranchWithId,"/projects/{1:projectId}/branches/{2:branchId}",drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::deleteBranch,"/projects/{1:projectId}/branches/{2:branchId}", drogon::Delete, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::getTags,"/projects/{1:projectId}/tags",drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::getTagsById,"/projects/{1:projectId}/tags/{2:tagId}",drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::postTag,"/projects/{1:projectId}/tags",drogon::Post, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::deleteTag,"/projects/{1:projectId}/tags/{2:tagId}",drogon::Delete, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::getDiff,"/projects/{1:projectId}/commits/{2:compareCommitId}/diff",drogon::Get, "StructuraSystems::Server::JwtFilter");
		ADD_METHOD_TO(CommitController::mergeIntoBranch,"/projects/{1:projectId}/branches/{2:targetBranchId}/merge",drogon::Post, "StructuraSystems::Server::JwtFilter");
		METHOD_LIST_END

		void getCommits(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto commits = ProjectVerService->getCommits(project);
			callback(paginate(request, commits,
				[](const std::shared_ptr<SysMLv2::REST::Commit>& commit) { return boost::uuids::to_string(commit->getId()); },
				[](const std::shared_ptr<SysMLv2::REST::Commit>& commit) { return commit->serializeToJson(); }));		}

		void postCommit(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto jsonObject = requireJson(request);
			if (jsonObject == nullptr)
			{
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}

			// The sysmllib CommitRequest silently swallows deserialization errors and yields DataVersions
			// without payload (e.g. for @types its deserializer does not know), so the structure is checked first.
			if (!jsonObject->isObject() || !(*jsonObject)["change"].isArray())
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit request."));
				return;
			}
			const auto& requestedChanges = (*jsonObject)["change"];
			if (requestedChanges.empty())
			{
				callback(jsonError(drogon::k400BadRequest, "A commit needs at least one change."));
				return;
			}
			std::vector<bool> isDeletion;
			for (const auto& change : requestedChanges)
			{
				if (!change.isObject())
				{
					callback(jsonError(drogon::k400BadRequest, "Invalid commit change."));
					return;
				}
				const auto& payload = change["payload"];
				if (payload.isNull())
				{
					// A deletion names the identity of the data and has no payload.
					const auto& identity = change["identity"];
					if (!identity.isObject() || !identity["@id"].isString() || !parseUuid(identity["@id"].asString()))
					{
						callback(jsonError(drogon::k400BadRequest, "A deletion needs the identity of the data."));
						return;
					}
					isDeletion.push_back(true);
				}
				else if (!payload.isObject() || !payload["@type"].isString())
				{
					callback(jsonError(drogon::k400BadRequest, "Invalid commit change."));
					return;
				}
				else
					isDeletion.push_back(false);
			}

			std::shared_ptr<SysMLv2::REST::CommitRequest> commitRequest;
			std::string commitDescription;
			std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> commitChanges;
			std::vector<boost::uuids::uuid> requestedPreviousCommits;
			try {
				commitRequest = std::make_shared<SysMLv2::REST::CommitRequest>(jsonObject->toStyledString());
				commitDescription = commitRequest->description();
				commitChanges = commitRequest->changeRequested();
				requestedPreviousCommits = commitRequest->previousCommits();
			}
			catch (...) {
				callback(jsonError(drogon::k400BadRequest, "Invalid commit request."));
				return;
			}

			if (commitChanges.size() != requestedChanges.size())
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit change."));
				return;
			}
			for (size_t index = 0; index < commitChanges.size(); index++)
			{
				const auto& change = commitChanges[index];
				if (change == nullptr)
				{
					callback(jsonError(drogon::k400BadRequest, "Invalid commit change."));
					return;
				}
				if (isDeletion[index])
				{
					if (change->getPayload() != nullptr)
					{
						callback(jsonError(drogon::k400BadRequest, "Invalid commit change."));
						return;
					}
				}
				else if (!isKnownData(change->getPayload()))
				{
					callback(jsonError(drogon::k400BadRequest, "The payload is not a known type of data."));
					return;
				}
			}

			const auto branch = selectBranch(request, project, callback);
			if (branch == nullptr)
				return;

			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits;
			for (const auto& previousId : requestedPreviousCommits)
			{
				const auto previous = ProjectVerService->getCommitById(project, previousId);
				if (previous == nullptr)
				{
					callback(jsonError(drogon::k404NotFound, "Previous commit not found."));
					return;
				}
				previousCommits.push_back(previous);
			}

			std::shared_ptr<SysMLv2::REST::Commit> commit;
			try {
				commit = ProjectVerService->createCommit(commitDescription, commitChanges, previousCommits, project, branch);
			}
			catch (const std::invalid_argument& exception) {
				// SysMLv2::REST::Versioning::InvalidCommitException is an std::invalid_argument.
				callback(jsonError(drogon::k400BadRequest, exception.what()));
				return;
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
				return;
			}

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created, drogon::CT_APPLICATION_JSON);
			response->setBody(commit->serializeToJson());
			callback(response);
		}

		void getBranchWithId(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& branchId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedBranchId = parseUuid(branchId);
			if (!parsedBranchId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid branch id."));
				return;
			}

			auto _branchId = *parsedBranchId;
			const auto branch = ProjectVerService->getBranchById(project, _branchId);

			if (branch==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Branch not found."));
                return;
            }

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(ProjectVerService->serializeBranch(branch));
			callback(response);
		}

		void getCommitId(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& commitId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;


			const auto parsedCommitId = parseUuid(commitId);
			if (!parsedCommitId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}

			auto _commitId = *parsedCommitId;
			const auto commit = ProjectVerService->getCommitById(project, _commitId);

			if (commit) {
				const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
				response->setBody(commit->serializeToJson());
				callback(response);
			}else {
				callback(jsonError(drogon::k404NotFound, "Commit not found."));
			}
		}

		void getCommitChange(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& commitId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedCommitId = parseUuid(commitId);
			if (!parsedCommitId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}

			auto _commitId = *parsedCommitId;
			const auto commit = ProjectVerService->getCommitById(project, _commitId);
			if (commit==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Commit not found."));
                return;
            }


			std::vector<int> changeTypes;
			if (!parseChangeTypes(request, changeTypes))
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid change type. Expected CREATED, UPDATED or DELETED."));
				return;
			}

			std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> change;
			try {
				change = ProjectVerService->getCommitChange(project, commit, changeTypes);
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
				return;
			}

			callback(paginate(request, change,
				[](const std::shared_ptr<SysMLv2::REST::DataVersion>& version) { return boost::uuids::to_string(version->getVersionId()); },
				[](const std::shared_ptr<SysMLv2::REST::DataVersion>& version) { return version->serializeToJson(); }));
		}

		void getCommitChangeById(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& commitId,const std::string& changeId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedCommitId = parseUuid(commitId);
			if (!parsedCommitId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}

			auto _commitId = *parsedCommitId;
			const auto commit = ProjectVerService->getCommitById(project, _commitId);
			if (commit==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Commit not found."));
                return;
            }


			const auto parsedChangeId = parseUuid(changeId);
			if (!parsedChangeId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid change id."));
				return;
			}

			auto _changeId = *parsedChangeId;
			const auto commitChange = ProjectVerService->getCommitChangeById(project,commit,_changeId);
			if (commitChange==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Change not found."));
                return;
            }


			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(commitChange->serializeToJson());
			callback(response);
		}

		void postBranch(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto jsonObject = requireJson(request);
			if (jsonObject == nullptr)
			{
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}

			std::shared_ptr<SysMLv2::REST::BranchRequest> branchReq;
			std::string branchName;
			std::shared_ptr<SysMLv2::REST::Commit> branchHead;
			try {
				branchReq = std::make_shared<SysMLv2::REST::BranchRequest>(jsonObject->toStyledString());
				branchName = branchReq->getName();
				branchHead = branchReq->getHead();
			}
			catch (...) {
				callback(jsonError(drogon::k400BadRequest, "Invalid branch request."));
				return;
			}
			if (branchName.empty())
			{
				callback(jsonError(drogon::k400BadRequest, "Branch name must not be empty."));
				return;
			}
			if (branchHead == nullptr)
			{
				callback(jsonError(drogon::k400BadRequest, "Branch head commit is required."));
				return;
			}

			// The request only carries a stub with the id; resolve the real commit of this project.
			const auto realHead = ProjectVerService->getCommitById(project, branchHead->getId());
			if (realHead == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Head commit not found."));
				return;
			}

			std::shared_ptr<SysMLv2::REST::Branch> branch;
			try {
				branch = ProjectVerService->createBranch(project, branchName, realHead);
			}
			catch (const BranchAlreadyExistsException& exception) {
				callback(jsonError(drogon::k409Conflict, exception.what()));
				return;
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
				return;
			}

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created, drogon::CT_APPLICATION_JSON);
			response->setBody(ProjectVerService->serializeBranch(branch));
			callback(response);
		}

		void deleteBranch(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& branchId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto parsedBranchId = parseUuid(branchId);
			if (!parsedBranchId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid branch id."));
				return;
			}

			auto branchID = *parsedBranchId;
			std::shared_ptr<SysMLv2::REST::Branch> branch;
			try {
				branch = ProjectVerService->deleteBranch(project,branchID);
			}
			catch (const DefaultBranchDeletionException& exception) {
				callback(jsonError(drogon::k409Conflict, exception.what()));
				return;
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
				return;
			}
			if (branch==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Branch not found."));
                return;
            }

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(ProjectVerService->serializeBranch(branch));
			callback(response);
		}

		void getBranches(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto branches = ProjectVerService->getBranches(project);

			callback(paginate(request, branches,
				[](const std::shared_ptr<SysMLv2::REST::Branch>& branch) { return boost::uuids::to_string(branch->getId()); },
				[this](const std::shared_ptr<SysMLv2::REST::Branch>& branch) { return ProjectVerService->serializeBranch(branch); }));
		}

		void getTags(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			auto tags = ProjectVerService->getTags(project);

			callback(paginate(request, tags,
				[](const std::shared_ptr<SysMLv2::REST::Tag>& tag) { return boost::uuids::to_string(tag->getId()); },
				[](const std::shared_ptr<SysMLv2::REST::Tag>& tag) { return tag->serializeToJson(); }));
		}

		void getTagsById(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& tagId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedTagId = parseUuid(tagId);
			if (!parsedTagId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid tag id."));
				return;
			}

			auto tagID = *parsedTagId;
			const auto& ptag = ProjectVerService->getTagById(project,tagID);
			if (ptag==nullptr)
            {
                callback(jsonError(drogon::k404NotFound, "Tag not found."));
                return;
            }



			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(ptag->serializeToJson());
			callback(response);
		}

		void postTag(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto jsonObject = requireJson(request);
			if (jsonObject == nullptr || !jsonObject->isObject())
			{
				callback(jsonError(drogon::k400BadRequest, "Missing or invalid JSON body."));
				return;
			}
			const auto& name = (*jsonObject)["name"];
			if (!name.isString() || name.asString().empty())
			{
				callback(jsonError(drogon::k400BadRequest, "Tag name must be a non-empty string."));
				return;
			}
			const auto& taggedCommit = (*jsonObject)["taggedCommit"];
			if (!taggedCommit.isObject() || !taggedCommit["@id"].isString())
			{
				callback(jsonError(drogon::k400BadRequest, "taggedCommit must be a reference {\"@id\": ...}."));
				return;
			}
			const auto taggedCommitId = parseUuid(taggedCommit["@id"].asString());
			if (!taggedCommitId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}

			const auto commit = ProjectVerService->getCommitById(project, *taggedCommitId);
			if (commit == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Tagged commit not found."));
				return;
			}

			std::string tagName = name.asString();
			std::shared_ptr<SysMLv2::REST::Tag> tag;
			try {
				tag = ProjectVerService->createTag(project, tagName, commit);
			}
			catch (const TagAlreadyExistsException& exception) {
				callback(jsonError(drogon::k409Conflict, exception.what()));
				return;
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
				return;
			}

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k201Created, drogon::CT_APPLICATION_JSON);
			response->setBody(tag->serializeToJson());
			callback(response);
		}

		void deleteTag(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& tagId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto parsedTagId = parseUuid(tagId);
			if (!parsedTagId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid tag id."));
				return;
			}

			auto tagUuid = *parsedTagId;
			std::shared_ptr<SysMLv2::REST::Tag> tag;
			try {
				tag = ProjectVerService->deleteTag(project, tagUuid);
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
				return;
			}
			if (tag == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Tag not found."));
				return;
			}

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(tag->serializeToJson());
			callback(response);
		}

		void getDiff(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& compareCommitId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Read, callback);
			if (!project)
				return;

			const auto parsedCompareId = parseUuid(compareCommitId);
			if (!parsedCompareId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid commit id."));
				return;
			}

			const auto baseValues = queryValues(request, "baseCommitId");
			if (baseValues.empty())
			{
				callback(jsonError(drogon::k400BadRequest, "The parameter baseCommitId is required."));
				return;
			}
			const auto parsedBaseId = parseUuid(baseValues.front());
			if (!parsedBaseId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid base commit id."));
				return;
			}

			std::vector<int> changeTypes;
			if (!parseChangeTypes(request, changeTypes))
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid change type. Expected CREATED, UPDATED or DELETED."));
				return;
			}

			const auto compareCommit = ProjectVerService->getCommitById(project, *parsedCompareId);
			if (compareCommit == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Commit not found."));
				return;
			}
			const auto baseCommit = ProjectVerService->getCommitById(project, *parsedBaseId);
			if (baseCommit == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Base commit not found."));
				return;
			}

			std::vector<std::shared_ptr<SysMLv2::REST::DataDifference>> differences;
			try {
				differences = ProjectVerService->diffCommits(baseCommit, compareCommit, changeTypes);
			}
			catch (const std::exception& exception) {
				callback(internalError(exception));
				return;
			}
			// A difference is identified by the identity of the data; each identity appears once.
			callback(paginate(request, differences,
				[](const std::shared_ptr<SysMLv2::REST::DataDifference>& difference) {
					const auto& version = difference->baseData() != nullptr ? difference->baseData() : difference->compareData();
					return boost::uuids::to_string(version->getId());
				},
				[](const std::shared_ptr<SysMLv2::REST::DataDifference>& difference) { return difference->serializeToJson(); }));
		}

		void mergeIntoBranch(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& targetBranchId)
		{
			const auto project = withProject(request, projectId, ProjectAccess::Write, callback);
			if (!project)
				return;

			const auto parsedBranchId = parseUuid(targetBranchId);
			if (!parsedBranchId)
			{
				callback(jsonError(drogon::k400BadRequest, "Invalid branch id."));
				return;
			}
			auto branchUuid = *parsedBranchId;
			const auto branch = ProjectVerService->getBranchById(project, branchUuid);
			if (branch == nullptr)
			{
				callback(jsonError(drogon::k404NotFound, "Branch not found."));
				return;
			}

			const auto sourceIds = queryValues(request, "sourceCommitId");
			if (sourceIds.empty())
			{
				callback(jsonError(drogon::k400BadRequest, "At least one sourceCommitId is required."));
				return;
			}
			std::vector<boost::uuids::uuid> parsedSourceIds;
			for (const auto& sourceId : sourceIds)
			{
				const auto parsed = parseUuid(sourceId);
				if (!parsed)
				{
					callback(jsonError(drogon::k400BadRequest, "Invalid source commit id."));
					return;
				}
				parsedSourceIds.push_back(*parsed);
			}

			// The optional body is the resolution: an array of Data that resolve the conflicts.
			std::vector<std::shared_ptr<SysMLv2::REST::Data>> resolution;
			if (!request->getBody().empty())
			{
				const auto jsonObject = requireJson(request);
				if (jsonObject == nullptr || !jsonObject->isArray())
				{
					callback(jsonError(drogon::k400BadRequest, "The body must be an array of data."));
					return;
				}
				for (const auto& entry : *jsonObject)
				{
					if (!entry.isObject())
					{
						callback(jsonError(drogon::k400BadRequest, "The body must be an array of data."));
						return;
					}
					std::shared_ptr<SysMLv2::REST::Data> data;
					try {
						data = std::dynamic_pointer_cast<SysMLv2::REST::Data>(SysMLv2::SysMLv2Deserializer::deserializeJsonString(entry.toStyledString()));
					}
					catch (...) {
						data = nullptr;
					}
					if (!isKnownData(data))
					{
						callback(jsonError(drogon::k400BadRequest, "The resolution contains data of an unknown type."));
						return;
					}
					resolution.push_back(data);
				}
			}

			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> sources;
			for (const auto& sourceId : parsedSourceIds)
			{
				const auto source = ProjectVerService->getCommitById(project, sourceId);
				if (source == nullptr)
				{
					callback(jsonError(drogon::k404NotFound, "Source commit not found."));
					return;
				}
				sources.push_back(source);
			}

			std::shared_ptr<SysMLv2::REST::MergeResult> result;
			try {
				result = ProjectVerService->mergeIntoBranch(project, branch, sources, resolution, request->getParameter("description"));
			}
			catch (const std::invalid_argument& exception) {
				callback(jsonError(drogon::k400BadRequest, exception.what()));
				return;
			}
			catch (const std::exception& exception) {
				// for example a broken history (std::runtime_error from versionedData) or the database
				callback(internalError(exception));
				return;
			}

			const auto status = result->mergeCommit() != nullptr ? drogon::k200OK : drogon::k409Conflict;
			const auto response = drogon::HttpResponse::newHttpResponse(status, drogon::CT_APPLICATION_JSON);
			response->setBody(result->serializeToJson());
			callback(response);
		}

	private:
		/// Data that the API stores: an Element of the KerML model, a ProjectUsage, an ExternalRelationship or ExternalData.
		/// The deserializer restores a KerML or SysML metaclass as an object of its class and every other @type as a plain Element
		/// that only keeps the @type; such an element is not accepted (unless its @type really is "Element").
		static bool isKnownData(const std::shared_ptr<SysMLv2::REST::Data>& data)
		{
			return data != nullptr
				&& (isKnownElement(std::dynamic_pointer_cast<KerML::Entities::Element>(data))
					|| std::dynamic_pointer_cast<SysMLv2::REST::ProjectUsage>(data) != nullptr
					|| std::dynamic_pointer_cast<SysMLv2::REST::ExternalRelationship>(data) != nullptr
					|| std::dynamic_pointer_cast<SysMLv2::REST::ExternalData>(data) != nullptr);
		}

		static bool isKnownElement(const std::shared_ptr<KerML::Entities::Element>& element)
		{
			return element != nullptr
				&& (typeid(*element) != typeid(KerML::Entities::Element) || element->getType() == "Element");
		}

		/// The branch a commit goes to: branchId, else the alias branch (by name), else the default branch. Answers 400/404
		/// through the callback and returns nullptr if there is none.
		std::shared_ptr<SysMLv2::REST::Branch> selectBranch(const drogon::HttpRequestPtr& request,
			const std::shared_ptr<Project>& project, const std::function<void(const drogon::HttpResponsePtr&)>& callback)
		{
			const auto branchIds = queryValues(request, "branchId");
			if (!branchIds.empty())
			{
				const auto parsed = parseUuid(branchIds.front());
				if (!parsed)
				{
					callback(jsonError(drogon::k400BadRequest, "Invalid branch id."));
					return nullptr;
				}
				auto branchUuid = *parsed;
				const auto branch = ProjectVerService->getBranchById(project, branchUuid);
				if (branch == nullptr)
					callback(jsonError(drogon::k404NotFound, "Branch not found."));
				return branch;
			}

			// Proprietary alias that the web interface uses: the branch by name.
			const auto branchNames = queryValues(request, "branch");
			if (!branchNames.empty())
			{
				for (const auto& candidate : ProjectVerService->getBranches(project))
					if (candidate->getName() == branchNames.front())
						return candidate;
				callback(jsonError(drogon::k404NotFound, "Branch not found."));
				return nullptr;
			}

			const auto branch = ProjectVerService->getDefaultBranch(project);
			if (branch == nullptr)
				callback(jsonError(drogon::k500InternalServerError, "The project has no default branch."));
			return branch;
		}

		std::shared_ptr<ProjectVersioningService> ProjectVerService = ProjectVersioningService::getInstance();
		std::shared_ptr<ServerProjectService> ProjectNavigationService = ServerProjectService::getInstance();
	};
}
