#include "ProjectVersioningService.h"

#include <kerml/root/annotations/TextualRepresentation.h>
#include <kerml/root/elements/Element.h>
#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/ChangeType.h>
#include <sysmlv2/rest/entities/Data.h>
#include <sysmlv2/rest/entities/Tag.h>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <shared_mutex>

#include "../Controller/CommitController.hpp"
#include "../Entities/db/Project.hpp"

namespace StructuraSystems::Server
{
	namespace
	{
		/// Commits are serialized (DataVersion::serializeToJson dereferences the payload), so a change
		/// without payload would crash the process. Throws std::invalid_argument instead.
		void requireValidChange(const std::shared_ptr<SysMLv2::REST::DataVersion>& change)
		{
			if (change == nullptr || change->getPayload() == nullptr)
				throw std::invalid_argument("Commit change without payload.");
		}
	}


	std::shared_ptr<ProjectVersioningService> ProjectVersioningService::getInstance()
	{
		struct concreteProjectVersioningService : public ProjectVersioningService {};
		// A function-local static is initialised exactly once, even with concurrent first calls.
		static const std::shared_ptr<ProjectVersioningService> instance = std::make_shared<concreteProjectVersioningService>();
		return instance;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Commit>> ProjectVersioningService::getCommits(
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdCommitMap.find(project->getId());
		if (it == ProjectIdCommitMap.end())
			return {};
		return it->second;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::getHeadCommit(
		std::shared_ptr<SysMLv2::REST::Project>, std::shared_ptr<SysMLv2::REST::Branch> branch)
	{
		std::shared_lock lock(Mutex);
		return branch->getHead();
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::getCommitById(
		std::shared_ptr<SysMLv2::REST::Project> project, boost::uuids::uuid commitId)
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdCommitMap.find(project->getId());
		if (it == ProjectIdCommitMap.end())
			return nullptr;

		for (const auto& commit : it->second)
		{
			if (commit->getId() == commitId)
				return commit;
		}
		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Branch> branch,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		requireValidChange(change);
		std::unique_lock lock(Mutex);
		return createCommitUnlocked(change, branch, previousCommits, project);
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommitUnlocked(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Branch> branch,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		requireValidChange(change);
		const auto commit = std::make_shared<SysMLv2::REST::Commit>("", project, previousCommits);
		commit->addChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		branch->setHead(commit);

		DBController->addCommit(commit);
		DBController->updateBranch(branch);

		return commit;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(std::string description,
		std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> change, std::shared_ptr<SysMLv2::REST::Project> project,
		std::shared_ptr<SysMLv2::REST::Branch> branch) {
		for (const auto& dataVersion : change)
			requireValidChange(dataVersion);
		std::unique_lock lock(Mutex);
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits;

		if (branch->getHead())
			previousCommits.push_back(branch->getHead());

		const auto commit = std::make_shared<SysMLv2::REST::Commit>(description, project, previousCommits);
		commit->setChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		branch->setHead(commit);

		DBController->addCommit(commit);
		DBController->updateBranch(branch);

		return commit;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Branch> branch,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		requireValidChange(change);
		std::unique_lock lock(Mutex);
		const auto commit = std::make_shared<SysMLv2::REST::Commit>("", project);
		commit->addChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		branch->setHead(commit);

		DBController->addCommit(commit);
		DBController->updateBranch(branch);

		return commit;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		requireValidChange(change);
		std::unique_lock lock(Mutex);
		const auto commit = std::make_shared<SysMLv2::REST::Commit>("", project, previousCommits);
		commit->addChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		DBController->addCommit(commit);

		for (const auto& branch : ProjectIdBranchMap[project->getId()]) {
			if (branch->getId() == project->getDefaultBranch()->getId()) {
				branch->setHead(commit);
				DBController->updateBranch(branch);
			}
		}

		return commit;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Project> project)
	{
		requireValidChange(change);
		std::unique_lock lock(Mutex);
		const auto commit = std::make_shared<SysMLv2::REST::Commit>("", project);
		commit->addChange(change);

		ProjectIdCommitMap[project->getId()].push_back(commit);
		DBController->addCommit(commit);

		for (const auto& branch : ProjectIdBranchMap[project->getId()]) {
			if (branch->getId() == project->getDefaultBranch()->getId()) {
				branch->setHead(commit);
				DBController->updateBranch(branch);
			}
		}

		return commit;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> ProjectVersioningService::getCommitChange(
		std::shared_ptr<SysMLv2::REST::Project> , std::shared_ptr<SysMLv2::REST::Commit> ,
		std::vector<int> )
	{
		std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> returnValue;

		// uint8_t filter = 0x00;

		// for (const auto change : changetype)
		// 	filter |= (uint8_t)change;

		// for (const auto& dataVersion : commit->getDataVersion())
		// {
		// }

		return returnValue;
	}

	std::shared_ptr<SysMLv2::REST::DataVersion> ProjectVersioningService::getCommitChangeById(
		std::shared_ptr<SysMLv2::REST::Project>, std::shared_ptr<SysMLv2::REST::Commit> commit,
		boost::uuids::uuid& changeId)
	{
		for (const auto& change : commit->getDataVersion())
		{
			if (change->getId() == changeId)
				return change;
		}
		return nullptr;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Branch>> ProjectVersioningService::getBranches(
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdBranchMap.find(project->getId());
		if (it == ProjectIdBranchMap.end())
			return {};
		return it->second;
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::getBranchById(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &branchId)
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdBranchMap.find(project->getId());
		if (it == ProjectIdBranchMap.end())
			return nullptr;

		for (const auto& branch : it->second)
			if (branch->getId() == branchId)
				return branch;

		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::getDefaultBranch(
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdBranchMap.find(project->getId());
		if (it == ProjectIdBranchMap.end() || project->getDefaultBranch() == nullptr)
			return nullptr;

		for (const auto& branch: it->second)
			if (branch->getId()==project->getDefaultBranch()->getId())
				return branch;
		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Project> ProjectVersioningService::setDefaultBranch(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &branchId)
	{
		std::unique_lock lock(Mutex);
		const auto branchesList = ProjectIdBranchMap.at(project->getId());

		for (const auto &branch : branchesList)
		{
			if (branch->getId()==branchId)
			{
				project->setDefaultBranch(branch);
				DBController->updateProject(std::dynamic_pointer_cast<StructuraSystems::Server::Project>(project));
				return project;
			}
		}
		throw std::runtime_error("Branch not found.");
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::createBranch(std::shared_ptr<SysMLv2::REST::Project> project, std::string branchName, std::shared_ptr<SysMLv2::REST::Commit> head)
	{
		if (head == nullptr)
			throw std::invalid_argument("A branch requires a head commit.");

		std::unique_lock lock(Mutex);
		// Duplicate check and creation happen under the same lock, so concurrent requests cannot both succeed.
		const auto existing = ProjectIdBranchMap.find(project->getId());
		if (existing != ProjectIdBranchMap.end())
		{
			for (const auto& candidate : existing->second)
			{
				if (candidate->getName() == branchName)
					throw BranchAlreadyExistsException();
			}
		}
		const auto newBranch = std::make_shared<SysMLv2::REST::Branch>(branchName);
		newBranch->setHead(head);
		DBController->addBranch(project->getId(), newBranch);
		ProjectIdBranchMap[project->getId()].push_back(newBranch);
		return newBranch;
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::deleteBranch(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &branchId)
	{
		std::unique_lock lock(Mutex);
		const auto it = ProjectIdBranchMap.find(project->getId());
		if (it == ProjectIdBranchMap.end())
			return nullptr;

		auto& branchesList = it->second;
		const auto branchIt = std::find_if(branchesList.begin(), branchesList.end(),
			[&branchId](const std::shared_ptr<SysMLv2::REST::Branch>& candidate) { return candidate->getId() == branchId; });
		if (branchIt == branchesList.end())
			return nullptr;

		const auto branch = *branchIt;
		if (project->getDefaultBranch() != nullptr && project->getDefaultBranch()->getId() == branchId)
			throw DefaultBranchDeletionException();

		branchesList.erase(branchIt);
		DBController->deleteBranch(branch);
		return branch;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::Tag>> ProjectVersioningService::getTags(std::shared_ptr<SysMLv2::REST::Project> project)
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdTagMap.find(project->getId());
		if (it == ProjectIdTagMap.end())
			return {};
		return it->second;
	}

	std::shared_ptr<SysMLv2::REST::Tag> ProjectVersioningService::getTagById(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &tagId)
	{
		std::shared_lock lock(Mutex);
		const auto it = ProjectIdTagMap.find(project->getId());
		if (it == ProjectIdTagMap.end())
			return nullptr;

		for (const auto& tag : it->second)
		{
			if (tag->getId() == tagId)
				return tag;
		}
		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::getTaggedCommit(std::shared_ptr<SysMLv2::REST::Project> , std::shared_ptr<SysMLv2::REST::Tag> tag)
	{
		return tag->referencedCommit();
	}

	std::shared_ptr<SysMLv2::REST::Tag> ProjectVersioningService::createTag(std::shared_ptr<SysMLv2::REST::Project> project,
			std::string &tagName, std::shared_ptr<SysMLv2::REST::Commit> taggedCommit)
	{
		std::unique_lock lock(Mutex);
		const auto newTag = std::make_shared<SysMLv2::REST::Tag>(tagName);
		newTag->setReferencedCommit(taggedCommit);
		ProjectIdTagMap[project->getId()].push_back(newTag);
		DBController->addTag(project->getId(), newTag);
		return newTag;
	}

	std::shared_ptr<SysMLv2::REST::MergeResult> ProjectVersioningService::mergeIntoBranch(std::shared_ptr<SysMLv2::REST::Branch> ,
			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> ,
			std::shared_ptr<SysMLv2::REST::Data> , std::string &)
	{
		return nullptr;
	}

	std::vector<std::shared_ptr<SysMLv2::REST::DataDifference>> ProjectVersioningService::diffCommits(
		std::shared_ptr<SysMLv2::REST::Commit>, std::shared_ptr<SysMLv2::REST::Commit>,
		std::vector<int>)
	{
		return std::vector<std::shared_ptr<SysMLv2::REST::DataDifference>>();
	}

	std::string ProjectVersioningService::serializeBranch(const std::shared_ptr<SysMLv2::REST::Branch>& branch)
	{
		// Branch::serializeToJson reads the head, which createCommit replaces under the unique lock.
		std::shared_lock lock(Mutex);
		return branch->serializeToJson();
	}

	void ProjectVersioningService::addBranchFromProject(std::shared_ptr<SysMLv2::REST::Project> project) {
		std::unique_lock lock(Mutex);
		ProjectIdBranchMap[project->getId()] = std::vector<std::shared_ptr<SysMLv2::REST::Branch>>();
		ProjectIdCommitMap[project->getId()] = std::vector<std::shared_ptr<SysMLv2::REST::Commit>>();

		ProjectIdBranchMap[project->getId()].push_back(project->getDefaultBranch());
		const auto dataVersion = std::make_shared<SysMLv2::REST::DataVersion>(boost::uuids::random_generator()(),std::make_shared<KerML::Entities::TextualRepresentation>("SysML v2","# To be filled by you"));
		createCommitUnlocked(dataVersion,project->getDefaultBranch(),{},project);
		DBController->addBranch(project->getId(), project->getDefaultBranch());
	}

	ProjectVersioningService::ProjectVersioningService()
	{
		DBController = DataBaseController::getInstance();

		ProjectIdBranchMap = DBController->getAllBranches();
		ProjectIdCommitMap = DBController->getAllCommits();
		ProjectIdTagMap = DBController->getAllTags();
	}
}
