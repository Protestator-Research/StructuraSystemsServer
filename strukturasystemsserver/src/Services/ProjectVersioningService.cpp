#include "ProjectVersioningService.h"

#include <sysmlv2/rest/entities/Branch.h>
#include <sysmlv2/rest/entities/Commit.h>
#include <sysmlv2/rest/entities/DataDifference.h>
#include <sysmlv2/rest/entities/DataIdentity.h>
#include <sysmlv2/rest/entities/DataVersion.h>
#include <sysmlv2/rest/entities/Project.h>
#include <sysmlv2/rest/entities/ChangeType.h>
#include <sysmlv2/rest/entities/Data.h>
#include <sysmlv2/rest/entities/MergeResult.h>
#include <sysmlv2/rest/entities/Tag.h>
#include <sysmlv2/rest/versioning/CommitFactory.h>
#include <sysmlv2/rest/versioning/Merge.h>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <shared_mutex>

#include "../Entities/db/Project.hpp"

namespace StructuraSystems::Server
{
	namespace
	{
		/// The ChangeType values arrive as ints through the interface of the library.
		std::vector<SysMLv2::REST::ChangeType> toChangeTypes(const std::vector<int>& values)
		{
			std::vector<SysMLv2::REST::ChangeType> result;
			for (const int value : values)
			{
				if (value != SysMLv2::REST::CREATED && value != SysMLv2::REST::UPDATED && value != SysMLv2::REST::DELETED)
					throw std::invalid_argument("Unknown change type.");
				result.push_back(static_cast<SysMLv2::REST::ChangeType>(value));
			}
			return result;
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
		const auto it = CommitIndex.find(commitId);
		if (it == CommitIndex.end())
			return nullptr;

		// A commit of another project is not found through this project.
		const auto owner = it->second->getOwningProject();
		if (owner == nullptr || owner->getId() != project->getId())
			return nullptr;
		return it->second;
	}

	SysMLv2::REST::Versioning::CommitResolver ProjectVersioningService::commitResolver() const
	{
		return [this](const boost::uuids::uuid& id) -> std::shared_ptr<SysMLv2::REST::Commit>
		{
			const auto it = CommitIndex.find(id);
			return it != CommitIndex.end() ? it->second : nullptr;
		};
	}

	std::shared_ptr<const SysMLv2::REST::Versioning::VersionedData> ProjectVersioningService::versionedDataUnlocked(
		const std::shared_ptr<SysMLv2::REST::Commit>& commit)
	{
		return withCache([&](auto* cache) { return SysMLv2::REST::Versioning::versionedData(commit, commitResolver(), cache); });
	}

	std::shared_ptr<const SysMLv2::REST::Versioning::VersionedData> ProjectVersioningService::getVersionedData(
		std::shared_ptr<SysMLv2::REST::Project>, std::shared_ptr<SysMLv2::REST::Commit> commit)
	{
		std::shared_lock lock(Mutex);
		return versionedDataUnlocked(commit);
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::storedBranchUnlocked(
		const std::shared_ptr<SysMLv2::REST::Project>& project, const std::shared_ptr<SysMLv2::REST::Branch>& branch,
		const boost::uuids::uuid* branchId) const
	{
		const auto it = ProjectIdBranchMap.find(project->getId());
		if (it == ProjectIdBranchMap.end())
			return nullptr;

		for (const auto& candidate : it->second)
		{
			if (branchId != nullptr ? candidate->getId() == *branchId
				: branch != nullptr ? candidate->getId() == branch->getId()
				: (project->getDefaultBranch() != nullptr && candidate->getId() == project->getDefaultBranch()->getId()))
				return candidate;
		}
		return nullptr;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::storeCommitUnlocked(const std::string& description,
		const std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>>& change,
		const std::vector<std::shared_ptr<SysMLv2::REST::Commit>>& previousCommits,
		const std::shared_ptr<SysMLv2::REST::Project>& project, const std::shared_ptr<SysMLv2::REST::Branch>& branch)
	{
		// The previous commits are the head of the branch and the requested ones; prepareCommit removes duplicates.
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previous;
		if (branch->getHead() != nullptr)
			previous.push_back(branch->getHead());
		previous.insert(previous.end(), previousCommits.begin(), previousCommits.end());

		std::shared_ptr<SysMLv2::REST::Commit> commit;
		{
			commit = withCache([&](auto* cache) {
				return SysMLv2::REST::Versioning::prepareCommit(project, description, change, previous, commitResolver(), cache);
			});
		}

		// Persist first: if the database refuses the commit, the service is unchanged.
		DBController->addCommit(commit);
		ProjectIdCommitMap[project->getId()].push_back(commit);
		CommitIndex[commit->getId()] = commit;
		const auto previousHead = branch->getHead();
		branch->setHead(commit);
		try {
			DBController->updateBranch(branch);
		}
		catch (...) {
			// The commit is stored and stays; only the head did not move.
			branch->setHead(previousHead);
			throw;
		}

		return commit;
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::string description, std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> change,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
		std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Branch> branch)
	{
		if (project == nullptr)
			throw std::invalid_argument("A commit needs a project.");

		std::unique_lock lock(Mutex);
		const auto storedBranch = storedBranchUnlocked(project, branch);
		if (storedBranch == nullptr)
			throw std::invalid_argument("The branch does not belong to the project.");

		return storeCommitUnlocked(description, change, previousCommits, project, storedBranch);
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Branch> branch,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		return createCommit("", {change}, previousCommits, project, branch);
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(std::string description,
		std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> change, std::shared_ptr<SysMLv2::REST::Project> project,
		std::shared_ptr<SysMLv2::REST::Branch> branch) {
		return createCommit(description, change, {}, project, branch);
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Branch> branch,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		return createCommit("", {change}, {}, project, branch);
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
		std::shared_ptr<SysMLv2::REST::Project> project)
	{
		return createCommit("", {change}, previousCommits, project, nullptr);
	}

	std::shared_ptr<SysMLv2::REST::Commit> ProjectVersioningService::createCommit(
		std::shared_ptr<SysMLv2::REST::DataVersion> change, std::shared_ptr<SysMLv2::REST::Project> project)
	{
		return createCommit("", {change}, {}, project, nullptr);
	}

	std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> ProjectVersioningService::getCommitChange(
		std::shared_ptr<SysMLv2::REST::Project> , std::shared_ptr<SysMLv2::REST::Commit> commit,
		std::vector<int> changeTypes)
	{
		const auto wantedTypes = toChangeTypes(changeTypes);
		std::shared_lock lock(Mutex);
		return withCache([&](auto* cache) { return SysMLv2::REST::Versioning::getCommitChange(commit, wantedTypes, commitResolver(), cache); });
	}

	std::shared_ptr<SysMLv2::REST::DataVersion> ProjectVersioningService::getCommitChangeById(
		std::shared_ptr<SysMLv2::REST::Project>, std::shared_ptr<SysMLv2::REST::Commit> commit,
		boost::uuids::uuid& changeId)
	{
		// The id of a change is the id of the DataVersion record, not the id of the Data (the identity).
		for (const auto& change : commit->getChange())
		{
			if (change->getVersionId() == changeId)
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
		const auto previous = replaceDefaultBranchUnlocked(project, branchId);
		try {
			DBController->updateProject(std::dynamic_pointer_cast<StructuraSystems::Server::Project>(project));
		}
		catch (...) {
			project->setDefaultBranch(previous);
			throw;
		}
		return project;
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::replaceDefaultBranchUnlocked(
		const std::shared_ptr<SysMLv2::REST::Project>& project, const boost::uuids::uuid& branchId)
	{
		const auto branch = storedBranchUnlocked(project, nullptr, &branchId);
		if (branch == nullptr)
			throw BranchNotFoundException();
		const auto previous = project->getDefaultBranch();
		project->setDefaultBranch(branch);
		return previous;
	}

	std::shared_ptr<SysMLv2::REST::Branch> ProjectVersioningService::replaceDefaultBranch(
		const std::shared_ptr<SysMLv2::REST::Project>& project, const boost::uuids::uuid& branchId)
	{
		std::unique_lock lock(Mutex);
		return replaceDefaultBranchUnlocked(project, branchId);
	}

	void ProjectVersioningService::restoreDefaultBranch(const std::shared_ptr<SysMLv2::REST::Project>& project,
		const std::shared_ptr<SysMLv2::REST::Branch>& branch)
	{
		std::unique_lock lock(Mutex);
		project->setDefaultBranch(branch);
	}

	void ProjectVersioningService::forgetProject(const std::shared_ptr<SysMLv2::REST::Project>& project)
	{
		std::unique_lock lock(Mutex);
		const auto projectId = project->getId();

		const auto commits = ProjectIdCommitMap.find(projectId);
		if (commits != ProjectIdCommitMap.end())
		{
			std::lock_guard cacheLock(CacheMutex);
			for (const auto& commit : commits->second)
			{
				CommitIndex.erase(commit->getId());
				DataCache.erase(commit->getId());
			}
			ProjectIdCommitMap.erase(commits);
		}

		// Branch -> Project and Project -> default Branch point at each other; cut the way back so the project can be freed.
		const auto branches = ProjectIdBranchMap.find(projectId);
		if (branches != ProjectIdBranchMap.end())
		{
			for (const auto& branch : branches->second)
				branch->setOwningProject(nullptr);
			ProjectIdBranchMap.erase(branches);
		}
		if (project->getDefaultBranch() != nullptr)
			project->getDefaultBranch()->setOwningProject(nullptr);

		const auto tags = ProjectIdTagMap.find(projectId);
		if (tags != ProjectIdTagMap.end())
		{
			for (const auto& tag : tags->second)
				tag->setOwningProject(nullptr);
			ProjectIdTagMap.erase(tags);
		}
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
		newBranch->setOwningProject(project);
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

		// The database first: if it refuses, the branch stays.
		DBController->deleteBranch(branch);
		branchesList.erase(branchIt);
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
		// Duplicate check and creation happen under the same lock, so concurrent requests cannot both succeed.
		const auto existing = ProjectIdTagMap.find(project->getId());
		if (existing != ProjectIdTagMap.end())
		{
			for (const auto& candidate : existing->second)
			{
				if (candidate->getName() == tagName)
					throw TagAlreadyExistsException();
			}
		}
		const auto newTag = std::make_shared<SysMLv2::REST::Tag>(tagName,project,taggedCommit);
		DBController->addTag(project->getId(), newTag);
		ProjectIdTagMap[project->getId()].push_back(newTag);
		return newTag;
	}

	std::shared_ptr<SysMLv2::REST::Tag> ProjectVersioningService::deleteTag(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &tagId)
	{
		std::unique_lock lock(Mutex);
		const auto it = ProjectIdTagMap.find(project->getId());
		if (it == ProjectIdTagMap.end())
			return nullptr;

		auto& tagList = it->second;
		const auto tagIt = std::find_if(tagList.begin(), tagList.end(),
			[&tagId](const std::shared_ptr<SysMLv2::REST::Tag>& candidate) { return candidate->getId() == tagId; });
		if (tagIt == tagList.end())
			return nullptr;

		const auto tag = *tagIt;
		DBController->deleteTag(tag);
		tagList.erase(tagIt);
		return tag;
	}

	std::shared_ptr<SysMLv2::REST::MergeResult> ProjectVersioningService::mergeIntoBranch(std::shared_ptr<SysMLv2::REST::Branch> baseBranch,
			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> commitsToMerge,
			std::shared_ptr<SysMLv2::REST::Data> resolution, std::string &description)
	{
		if (baseBranch == nullptr || baseBranch->getOwningProject() == nullptr)
			throw std::invalid_argument("The branch has no owning project.");

		std::vector<std::shared_ptr<SysMLv2::REST::Data>> resolutions;
		if (resolution != nullptr)
			resolutions.push_back(resolution);
		return mergeIntoBranch(baseBranch->getOwningProject(), baseBranch, commitsToMerge, resolutions, description);
	}

	std::shared_ptr<SysMLv2::REST::MergeResult> ProjectVersioningService::mergeIntoBranch(
		std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Branch> baseBranch,
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> commitsToMerge,
		std::vector<std::shared_ptr<SysMLv2::REST::Data>> resolution, std::string description)
	{
		if (project == nullptr || baseBranch == nullptr)
			throw std::invalid_argument("A merge needs a project and a branch.");

		std::unique_lock lock(Mutex);
		const auto branch = storedBranchUnlocked(project, baseBranch);
		if (branch == nullptr)
			throw std::invalid_argument("The branch does not belong to the project.");

		// The commits to merge are the stored commits of this project.
		std::vector<std::shared_ptr<SysMLv2::REST::Commit>> sources;
		for (const auto& reference : commitsToMerge)
		{
			const auto found = reference != nullptr ? CommitIndex.find(reference->getId()) : CommitIndex.end();
			if (found == CommitIndex.end() || found->second->getOwningProject() == nullptr
				|| found->second->getOwningProject()->getId() != project->getId())
				throw SysMLv2::REST::Versioning::InvalidCommitException("A commit to merge does not belong to the project.");
			sources.push_back(found->second);
		}

		SysMLv2::REST::Versioning::MergeAnalysis analysis;
		{
			analysis = withCache([&](auto* cache) {
				return SysMLv2::REST::Versioning::analyzeMerge(branch->getHead(), sources, resolution, commitResolver(), cache);
			});
		}
		if (!analysis.conflicts.empty())
			return std::make_shared<SysMLv2::REST::MergeResult>(nullptr, analysis.conflicts);

		// The head comes first among the previous commits (storeCommitUnlocked adds it), the merged commits follow.
		const auto mergeCommit = storeCommitUnlocked(description, analysis.changes, sources, project, branch);
		return std::make_shared<SysMLv2::REST::MergeResult>(mergeCommit);
	}

	std::vector<std::shared_ptr<SysMLv2::REST::DataDifference>> ProjectVersioningService::diffCommits(
		std::shared_ptr<SysMLv2::REST::Commit> baseCommit, std::shared_ptr<SysMLv2::REST::Commit> compareCommit,
		std::vector<int> changeTypes)
	{
		const auto wantedTypes = toChangeTypes(changeTypes);
		std::shared_lock lock(Mutex);
		return withCache([&](auto* cache) {
			return SysMLv2::REST::Versioning::diffCommits(baseCommit, compareCommit, wantedTypes, commitResolver(), cache);
		});
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

		// A new project has no commit: its default branch has no head until the first commit.
		DBController->addBranch(project->getId(), project->getDefaultBranch());
		ProjectIdBranchMap[project->getId()].push_back(project->getDefaultBranch());
	}

	ProjectVersioningService::ProjectVersioningService()
	{
		DBController = DataBaseController::getInstance();

		ProjectIdBranchMap = DBController->getAllBranches();
		ProjectIdCommitMap = DBController->getAllCommits();
		ProjectIdTagMap = DBController->getAllTags();

		for (const auto& [projectId, commits] : ProjectIdCommitMap)
			for (const auto& commit : commits)
				CommitIndex[commit->getId()] = commit;
	}
}
