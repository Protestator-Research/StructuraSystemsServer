#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Entities/db/ApiToken.hpp"

namespace StructuraSystems::Server
{
	class ApiTokenNameTakenException : public std::runtime_error {
	public:
		explicit ApiTokenNameTakenException(const std::string& name) : std::runtime_error("API token name already in use: " + name) {}
	};

	class ApiTokenLimitException : public std::runtime_error {
	public:
		ApiTokenLimitException() : std::runtime_error("Maximum number of API tokens reached") {}
	};

	/// Result of ApiTokenStore::revoke. In both revoked cases the token is invalid immediately in this process.
	enum class ApiTokenRevokeResult {
		Revoked,
		/// Removed from memory, but the database delete failed; it is retried (housekeeping, shutdown, repeated revoke).
		RevokedDatabasePending,
		NotFound
	};

	/// A freshly issued token. The plaintext is only available here and is never stored.
	struct CreatedApiToken {
		ApiToken Token;
		std::string Plaintext;
	};

	/// Thread safe store of API tokens, keyed by the SHA-256 hash of the token. create and removeProject write to the
	/// database first, then to memory; revoke and purgeExpired change memory first, then the database. create and
	/// removeProject hold the store mutex during their database calls, so validate and revoke wait while the database is slow.
	/// revoke, purgeExpired, flushLastUsed and retryPendingRevocations call the database without the mutex.
	class ApiTokenStore
	{
	public:
		/// Returns the current time in unix seconds.
		using NowFunction = std::function<std::int64_t()>;

		static constexpr const char* TOKEN_PREFIX = "sst_";
		/// "sst_" plus 32 random bytes as base64url without padding (43 characters).
		static constexpr std::size_t TOKEN_LENGTH = 47;

		/// Loads all tokens from the database. Without a clock, std::chrono::system_clock is used.
		explicit ApiTokenStore(NowFunction now = {});

		/// Expired tokens are kept this long (a wrong system clock must not delete tokens), then purged.
		static constexpr std::int64_t EXPIRED_RETENTION_SECONDS = 30LL * 24 * 60 * 60;

		/// Issues a token valid for lifetimeSeconds. Throws ApiTokenNameTakenException, ApiTokenLimitException
		/// (maxCount unexpired tokens exist) or a database exception. A name held by an
		/// expired token is reused: that one token is deleted (database first). If the insert then fails, the expired token
		/// stays deleted, which is consistent. Expired tokens do not count against maxCount.
		CreatedApiToken create(const std::string& name, ApiTokenTarget targetType, std::vector<std::string> projects,
			const std::string& group, ApiTokenScope scope, const std::string& createdBy,
			std::int64_t lifetimeSeconds, std::size_t maxCount);

		/// Returns the token for a valid plaintext. Wrong format, unknown and expired tokens give nullopt.
		/// lastUsedAt is only updated in memory, flushLastUsed writes it to the database.
		std::optional<ApiToken> validate(const std::string& plaintext);

		std::vector<ApiToken> list() const;
		std::optional<ApiToken> findById(const std::string& id) const;

		/// True if the token is expired (validate rejects it).
		bool isExpired(const ApiToken& token) const;

		/// Removes the token from memory first (invalid at once), then deletes the document without holding the mutex.
		/// If that fails, the id is remembered and the delete retried by retryPendingRevocations or a repeated revoke.
		ApiTokenRevokeResult revoke(const std::string& id);

		/// Retries the database deletes of revoked tokens. Failures are logged and kept for the next call.
		void retryPendingRevocations();

		/// Ids revoked in memory whose database delete has not gone through yet.
		std::vector<std::string> pendingRevocations() const;

		/// Removes the project from all project tokens; a token left without a project is deleted.
		/// Returns the number of tokens changed or deleted.
		std::size_t removeProject(const std::string& projectId);

		/// Writes the lastUsedAt values changed since the last call to the database.
		void flushLastUsed();

		/// Deletes tokens that expired more than EXPIRED_RETENTION_SECONDS ago from memory and from the database. Returns their
		/// number (in memory). A database failure is logged and not thrown (memory is clean anyway); a later purge retries.
		std::size_t purgeExpired();

		std::size_t size() const;

	private:
		std::int64_t now() const;

		mutable std::mutex Mutex;
		NowFunction Now;
		std::unordered_map<std::string, ApiToken> Tokens; // token hash -> token
		std::unordered_set<std::string> Dirty; // token hashes with an unwritten lastUsedAt
		std::unordered_set<std::string> PendingRevocations; // ids revoked in memory whose document is not yet deleted
	};
}
