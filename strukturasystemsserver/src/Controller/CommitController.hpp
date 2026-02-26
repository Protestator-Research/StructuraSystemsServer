#pragma once

#include <drogon/HttpController.h>

#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/CommitRequest.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/BranchRequest.h>
#include <sysmlv2/rest/entities/Tag.h>

#include <string>

#include "../Services/ProjectVersioningService.h"
#include "../Services/ServerProjectService.h"
#include "../Filter/JwtFilter.hpp"

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
		METHOD_LIST_END

		void getCommits(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, std::string projectId)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			const auto& commits = ProjectVerService->getCommits(project);
			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < commits.size(); i++)
			{
				returnValue += commits[i]->serializeToJson();
				if (i < (commits.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";
			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(returnValue);
			callback(response);
		}

		void postCommit(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const std::string json = request->getJsonObject()->toStyledString();
			const auto commitRequest = std::make_shared<SysMLv2::REST::CommitRequest>(json);
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			auto branch = ProjectVerService->getDefaultBranch(project);

			if (branch==nullptr) {
				const auto response = drogon::HttpResponse::newHttpResponse(drogon::k500InternalServerError, drogon::CT_APPLICATION_JSON);
				callback(response);
				return;
			}

			auto branchName = request->getParameter("branch");
			if (!branchName.empty())
			{
				const auto branches = ProjectVerService->getBranches(project);
				for (const auto& _branch : branches)
					if (_branch->getName()==branchName)
						branch = _branch;
			}
			const auto commit = ProjectVerService->createCommit(commitRequest->description(), commitRequest->changeRequested(), project, branch);

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(commit->serializeToJson());
			callback(response);
		}

		void getBranchWithId(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& branchId)
		{
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			auto _branchId = boost::uuids::string_generator()(branchId);
			const auto branch = ProjectVerService->getBranchById(project, _branchId);

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(branch->serializeToJson());
			callback(response);
		}

		void getCommitId(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& commitId)
		{
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			auto _commitId = boost::uuids::string_generator()(commitId);
			const auto commit = ProjectVerService->getCommitById(project, _commitId);

			if (commit) {
				const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
				response->setBody(commit->serializeToJson());
				callback(response);
			}else if(!commit) {
				const auto response = drogon::HttpResponse::newHttpResponse(drogon::k404NotFound, drogon::CT_APPLICATION_JSON);
				response->setBody("Commit could not be found.");
				callback(response);
			}
		}

		void getCommitChange(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& commitId)
		{
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			auto _commitId = boost::uuids::string_generator()(commitId);
			const auto commit = ProjectVerService->getCommitById(project, _commitId);
			std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> change;

			auto changeType = request->getParameter("change_type");

			if (!changeType.empty()) {
				std::vector<int> changeTypes;
				if (changeType.find("CREATED")!=std::string::npos)
					changeTypes.push_back(SysMLv2::REST::CREATED);
				if (changeType.find("UPDATED")!=std::string::npos)
					changeTypes.push_back(SysMLv2::REST::UPDATED);
				if (changeType.find("DELETED")!=std::string::npos)
					changeTypes.push_back(SysMLv2::REST::DELETED);

				change = ProjectVerService->getCommitChange(project,commit,changeTypes);
			}else {
				change = ProjectVerService->getCommitChange(project,commit,{SysMLv2::REST::CREATED,SysMLv2::REST::DELETED,SysMLv2::REST::UPDATED});
			}

			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < change.size(); i++)
			{
				returnValue += change[i]->serializeToJson();
				if (i < (change.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(returnValue);
			callback(response);
		}

		void getCommitChangeById(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& commitId,const std::string& changeId)
		{
			const auto project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			auto _commitId = boost::uuids::string_generator()(commitId);
			const auto commit = ProjectVerService->getCommitById(project, _commitId);
			auto _changeId = boost::uuids::string_generator()(changeId);
			const auto commitChange = ProjectVerService->getCommitChangeById(project,commit,_changeId);

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(commitChange->serializeToJson());
			callback(response);
		}

		void postBranch(const drogon::HttpRequestPtr &request, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const std::string json = request->getJsonObject()->toStyledString();
			const auto branchReq = std::make_shared<SysMLv2::REST::BranchRequest>(json);
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			const auto& branch = ProjectVerService->createBranch(project, branchReq->getName(),branchReq->getHead());

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(branch->serializeToJson());
			callback(response);
		}

		void deleteBranch(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& branchId)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			auto branchID = boost::uuids::string_generator()(branchId);
			const auto branch = ProjectVerService->deleteBranch(project,branchID);

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(branch->serializeToJson());
			callback(response);
		}

		void getBranches(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			const auto branches = ProjectVerService->getBranches(project);

			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < branches.size(); i++)
			{
				returnValue += branches[i]->serializeToJson();
				if (i < (branches.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(returnValue);
			callback(response);
		}

		void getTags(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			auto tags = ProjectVerService->getTags(project);

			std::string returnValue = "[\r\n";
			for (size_t i = 0; i < tags.size(); i++)
			{
				returnValue += tags[i]->serializeToJson();
				if (i < (tags.size() - 1))
					returnValue += ",\r\n";
			}
			returnValue += "]";

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(returnValue);
			callback(response);
		}

		void getTagsById(const drogon::HttpRequestPtr &, std::function<void(const drogon::HttpResponsePtr&)>&& callback, const std::string& projectId, const std::string& tagId)
		{
			const auto& project = ProjectNavigationService->getProjectById(boost::uuids::string_generator()(projectId));
			auto tagID = boost::uuids::string_generator()(tagId);
			const auto& ptag = ProjectVerService->getTagById(project,tagID);

			const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
			response->setBody(ptag->serializeToJson());
			callback(response);
		}

	private:
		std::shared_ptr<ProjectVersioningService> ProjectVerService = ProjectVersioningService::getInstance();
		std::shared_ptr<ServerProjectService> ProjectNavigationService = ServerProjectService::getInstance();
	};
}
