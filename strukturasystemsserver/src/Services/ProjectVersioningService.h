#pragma once

#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <stdexcept>
#include <boost/uuid.hpp>
#include <sysmlv2/service/interfaces/IProjectVersioningService.h>
#include <sysmlv2/rest/entities/ChangeType.h>
#include <sysmlv2/rest/versioning/VersionedData.h>
#include "../Controller/DataBaseController.h"

namespace StructuraSystems::Server
{
	/// Thrown when the default branch of a project should be deleted.
	class DefaultBranchDeletionException : public std::runtime_error
	{
	public:
		DefaultBranchDeletionException() : std::runtime_error("The default branch of a project cannot be deleted.") {}
	};

	/// Thrown when a branch with the same name already exists within the project.
	class BranchAlreadyExistsException : public std::runtime_error
	{
	public:
		BranchAlreadyExistsException() : std::runtime_error("A branch with this name already exists.") {}
	};

	/// Thrown when a branch is not a branch of the project.
	class BranchNotFoundException : public std::runtime_error
	{
	public:
		BranchNotFoundException() : std::runtime_error("Branch not found.") {}
	};

	/// Thrown when a tag with the same name already exists within the project.
	class TagAlreadyExistsException : public std::runtime_error
	{
	public:
		TagAlreadyExistsException() : std::runtime_error("A tag with this name already exists.") {}
	};

	/// A commit that violates the invariants of the data versioning (deletion of unknown data, duplicate identity, a previous
	/// commit of another project, ...) is rejected with SysMLv2::REST::Versioning::InvalidCommitException, an
	/// std::invalid_argument.
	class ProjectVersioningService : public SysMLv2::API::IProjectVersioningService
	{
	public:
		static std::shared_ptr<ProjectVersioningService> getInstance();

		~ProjectVersioningService() override = default;

		std::vector<std::shared_ptr<SysMLv2::REST::Commit>>
		getCommits(std::shared_ptr<SysMLv2::REST::Project> project) override;

		std::shared_ptr<SysMLv2::REST::Commit> getHeadCommit(std::shared_ptr<SysMLv2::REST::Project> project,
			std::shared_ptr<SysMLv2::REST::Branch> branch) override;

		std::shared_ptr<SysMLv2::REST::Commit> getCommitById(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid commitId) override;

		std::shared_ptr<SysMLv2::REST::Commit> createCommit(std::shared_ptr<SysMLv2::REST::DataVersion> change,
			std::shared_ptr<SysMLv2::REST::Branch> branch,
			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
			std::shared_ptr<SysMLv2::REST::Project> project) override;

		/// The one implementation of createCommit (7.2.3). The previous commits of the new commit are the head of the branch
		/// (if it has one) and the given previousCommits, without duplicates. A branch of nullptr is the default branch of the project.
		/// The change is checked against the versionedData of the previous commits and normalized (see
		/// SysMLv2::REST::Versioning::prepareCommit). The head of the branch moves to the new commit.
		/// @throws SysMLv2::REST::Versioning::InvalidCommitException If the commit violates an invariant.
		/// @throws std::invalid_argument If the branch is not a branch of the project.
		std::shared_ptr<SysMLv2::REST::Commit> createCommit(std::string description,
			std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> change,
			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
			std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Branch> branch);

		std::shared_ptr<SysMLv2::REST::Commit> createCommit(std::string description,std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>>change, std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Branch> branch);

		std::shared_ptr<SysMLv2::REST::Commit> createCommit(std::shared_ptr<SysMLv2::REST::DataVersion> change,
			std::shared_ptr<SysMLv2::REST::Branch> branch, std::shared_ptr<SysMLv2::REST::Project> project) override;

		std::shared_ptr<SysMLv2::REST::Commit> createCommit(std::shared_ptr<SysMLv2::REST::DataVersion> change,
			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> previousCommits,
			std::shared_ptr<SysMLv2::REST::Project> project) override;

		std::shared_ptr<SysMLv2::REST::Commit> createCommit(std::shared_ptr<SysMLv2::REST::DataVersion> change,
			std::shared_ptr<SysMLv2::REST::Project> project) override;

		/// Commit.versionedData: the data that exists at the commit, by identity (7.1.2). Computed iteratively and cached per commit.
		std::shared_ptr<const SysMLv2::REST::Versioning::VersionedData> getVersionedData(
			std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit);

		/// The change of the commit. The change types are SysMLv2::REST::ChangeType values, empty means all of them.
		/// @throws std::invalid_argument If a value is no ChangeType.
		std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>> getCommitChange(
			std::shared_ptr<SysMLv2::REST::Project> project, std::shared_ptr<SysMLv2::REST::Commit> commit,
			std::vector<int> changeTypes) override;

		std::shared_ptr<SysMLv2::REST::DataVersion> getCommitChangeById(std::shared_ptr<SysMLv2::REST::Project> project,
			std::shared_ptr<SysMLv2::REST::Commit> commit, boost::uuids::uuid &changeId) override;

		std::vector<std::shared_ptr<SysMLv2::REST::Branch>>
		getBranches(std::shared_ptr<SysMLv2::REST::Project> project) override;

		std::shared_ptr<SysMLv2::REST::Branch> getBranchById(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &branchId) override;

		std::shared_ptr<SysMLv2::REST::Branch>
		getDefaultBranch(std::shared_ptr<SysMLv2::REST::Project> project) override;

		/// Sets the default branch and stores the project. The existence of the branch and the change happen under one lock; if the
		/// database refuses the project, the old default branch is restored.
		/// @throws BranchNotFoundException If the project has no branch with this id.
		std::shared_ptr<SysMLv2::REST::Project> setDefaultBranch(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &branchId) override;

		/// Makes the branch the default branch in memory only; the caller stores the project (ServerProjectService::updateProject, which
		/// holds its own lock, so the project is not read by anyone else while the field changes). Returns the previous default branch.
		/// @throws BranchNotFoundException If the project has no branch with this id.
		std::shared_ptr<SysMLv2::REST::Branch> replaceDefaultBranch(const std::shared_ptr<SysMLv2::REST::Project>& project,
			const boost::uuids::uuid& branchId);

		/// Puts the default branch back after a failed update.
		void restoreDefaultBranch(const std::shared_ptr<SysMLv2::REST::Project>& project, const std::shared_ptr<SysMLv2::REST::Branch>& branch);

		/// Forgets everything the service keeps for a deleted project: commits (index and cached versionedData), branches and tags
		/// from memory, and the link from the branches back to the project (which would keep the project alive). The database keeps the
		/// commits (they are not destructible); the next start loads them again.
		void forgetProject(const std::shared_ptr<SysMLv2::REST::Project>& project);

		std::shared_ptr<SysMLv2::REST::Branch> createBranch(std::shared_ptr<SysMLv2::REST::Project> project,
			std::string branchName, std::shared_ptr<SysMLv2::REST::Commit> head) override;

		std::shared_ptr<SysMLv2::REST::Branch> deleteBranch(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &branchId) override;

		std::vector<std::shared_ptr<SysMLv2::REST::Tag>>
		getTags(std::shared_ptr<SysMLv2::REST::Project> project) override;

		std::shared_ptr<SysMLv2::REST::Tag> getTagById(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &tagId) override;

		std::shared_ptr<SysMLv2::REST::Commit> getTaggedCommit(std::shared_ptr<SysMLv2::REST::Project> project,
			std::shared_ptr<SysMLv2::REST::Tag> tag) override;

		/// @throws TagAlreadyExistsException If the project has a tag with this name.
		std::shared_ptr<SysMLv2::REST::Tag> createTag(std::shared_ptr<SysMLv2::REST::Project> project,
			std::string &tagName, std::shared_ptr<SysMLv2::REST::Commit> taggedCommit) override;

		/// Deletes the tag. Returns nullptr if the project has no such tag.
		std::shared_ptr<SysMLv2::REST::Tag> deleteTag(std::shared_ptr<SysMLv2::REST::Project> project,
			boost::uuids::uuid &tagId);

		/// The merge of the interface; the branch has to know its owning project.
		std::shared_ptr<SysMLv2::REST::MergeResult> mergeIntoBranch(std::shared_ptr<SysMLv2::REST::Branch> baseBranch,
			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> commitsToMerge,
			std::shared_ptr<SysMLv2::REST::Data> resolution, std::string &description) override;

		/// Merges the commits into the branch (7.2.3). A conflict gives a MergeResult without commit that lists the conflicting
		/// identities; the branch is unchanged then. Otherwise the new commit has the head and the merged commits as previous
		/// commits and the branch moves to it.
		/// @param resolution Data that resolve conflicts, by their id.
		/// @throws SysMLv2::REST::Versioning::InvalidCommitException If a commit to merge is not a commit of the project.
		/// @throws std::invalid_argument If the branch is not a branch of the project.
		std::shared_ptr<SysMLv2::REST::MergeResult> mergeIntoBranch(std::shared_ptr<SysMLv2::REST::Project> project,
			std::shared_ptr<SysMLv2::REST::Branch> baseBranch,
			std::vector<std::shared_ptr<SysMLv2::REST::Commit>> commitsToMerge,
			std::vector<std::shared_ptr<SysMLv2::REST::Data>> resolution, std::string description);

		std::vector<std::shared_ptr<SysMLv2::REST::DataDifference>> diffCommits(
			std::shared_ptr<SysMLv2::REST::Commit> baseCommit, std::shared_ptr<SysMLv2::REST::Commit> compareCommit,
			std::vector<int> changeType) override;

		/// Serialises the branch while holding the read lock, so a concurrent createCommit cannot
		/// replace the head while it is read.
		std::string serializeBranch(const std::shared_ptr<SysMLv2::REST::Branch>& branch);

		/// Registers the default branch of a new project. The branch has no head and the project no commit.
		/// Must not call back into ServerProjectService (lock order: ServerProjectService -> ProjectVersioningService).
		void addBranchFromProject(std::shared_ptr<SysMLv2::REST::Project> project);

	private:
		ProjectVersioningService();

		/// Implementation of createCommit and mergeIntoBranch. The caller holds Mutex exclusively.
		std::shared_ptr<SysMLv2::REST::Commit> storeCommitUnlocked(const std::string& description,
			const std::vector<std::shared_ptr<SysMLv2::REST::DataVersion>>& change,
			const std::vector<std::shared_ptr<SysMLv2::REST::Commit>>& previousCommits,
			const std::shared_ptr<SysMLv2::REST::Project>& project, const std::shared_ptr<SysMLv2::REST::Branch>& branch);

		/// The stored branch for a branch of the caller; the default branch of the project for nullptr. nullptr if the project has
		/// no such branch. The caller holds Mutex.
		/// With branchId the branch with that id is looked for instead.
		std::shared_ptr<SysMLv2::REST::Branch> storedBranchUnlocked(const std::shared_ptr<SysMLv2::REST::Project>& project,
			const std::shared_ptr<SysMLv2::REST::Branch>& branch, const boost::uuids::uuid* branchId = nullptr) const;

		std::shared_ptr<SysMLv2::REST::Branch> replaceDefaultBranchUnlocked(const std::shared_ptr<SysMLv2::REST::Project>& project,
			const boost::uuids::uuid& branchId);

		/// Finds a stored commit by id. The caller holds Mutex.
		SysMLv2::REST::Versioning::CommitResolver commitResolver() const;

		/// versionedData of a stored commit, through the cache. The caller holds Mutex (shared or exclusive).
		std::shared_ptr<const SysMLv2::REST::Versioning::VersionedData> versionedDataUnlocked(
			const std::shared_ptr<SysMLv2::REST::Commit>& commit);

		/// Guards the three maps and the Branch/Commit objects stored in them. Not recursive.
		mutable std::shared_mutex Mutex;

		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Commit>>> ProjectIdCommitMap;
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Branch>>> ProjectIdBranchMap;
		std::map<boost::uuids::uuid, std::vector<std::shared_ptr<SysMLv2::REST::Tag>>> ProjectIdTagMap;

		/// All stored commits by id (a commit id is a random UUID, so it is unique over all projects). The previous commits of a
		/// commit that was read from the database are stubs, the versioning logic finds the real commits here.
		std::map<boost::uuids::uuid, std::shared_ptr<SysMLv2::REST::Commit>> CommitIndex;

		/// Upper limit of DataCache. If an operation leaves more entries, the cache is emptied; a state is then computed again.
		static constexpr size_t MAX_CACHED_STATES = 256;

		/// Runs a library function with the cache. Takes CacheMutex (the caller holds Mutex) and empties the cache if it got too big.
		template<typename Function>
		auto withCache(Function&& function)
		{
			std::lock_guard cacheLock(CacheMutex);
			struct Trim {
				SysMLv2::REST::Versioning::VersionedDataCache& Cache;
				~Trim() { if (Cache.size() > MAX_CACHED_STATES) Cache.clear(); }
			} trim{DataCache};
			return function(&DataCache);
		}

		/// versionedData per commit id. Commits are immutable, so an entry never becomes invalid. It is not cleared when a
		/// project is deleted, like the commits themselves. Readers hold Mutex shared, so the cache has its own mutex.
		/// Lock order: Mutex before CacheMutex.
		SysMLv2::REST::Versioning::VersionedDataCache DataCache;
		mutable std::mutex CacheMutex;

		DataBaseController* DBController;
	};

}
