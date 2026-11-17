#include "ApiTokenStore.h"

#include "../Controller/DataBaseController.h"

#include <mongocxx/exception/operation_exception.hpp>
#include <sodium.h>
#include <boost/uuid/random_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <limits>

namespace StructuraSystems::Server
{
	namespace
	{
		constexpr std::size_t TOKEN_BYTES = 32;

		std::string sha256Hex(const std::string& value)
		{
			std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
			crypto_hash_sha256(digest.data(), reinterpret_cast<const unsigned char*>(value.data()), value.size());
			std::string hex(digest.size() * 2 + 1, '\0');
			sodium_bin2hex(hex.data(), hex.size(), digest.data(), digest.size());
			hex.resize(digest.size() * 2);
			return hex;
		}

		std::string generatePlaintext()
		{
			std::array<unsigned char, TOKEN_BYTES> raw{};
			randombytes_buf(raw.data(), raw.size());

			std::string encoded(sodium_base64_ENCODED_LEN(raw.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
			sodium_bin2base64(encoded.data(), encoded.size(), raw.data(), raw.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
			// sodium_bin2base64 writes a terminating NUL that is part of the computed length.
			encoded.resize(encoded.find('\0'));
			sodium_memzero(raw.data(), raw.size());
			return ApiTokenStore::TOKEN_PREFIX + encoded;
		}
	}

	ApiTokenStore::ApiTokenStore(NowFunction now)
		: Now(std::move(now))
	{
		if (sodium_init() < 0)
			throw std::runtime_error("Failed to initialize libsodium");

		for (auto& token : DataBaseController::getInstance()->getAllApiTokens()) {
			const auto hash = token.tokenHash();
			Tokens.insert_or_assign(hash, std::move(token));
		}
	}

	std::int64_t ApiTokenStore::now() const
	{
		if (Now)
			return Now();
		return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	}

	CreatedApiToken ApiTokenStore::create(const std::string& name, ApiTokenTarget targetType, std::vector<std::string> projects,
		const std::string& group, ApiTokenScope scope, const std::string& createdBy,
		std::int64_t lifetimeSeconds, std::size_t maxCount)
	{
		ApiToken::validateTarget(targetType, projects, group);
		if (lifetimeSeconds <= 0)
			throw std::invalid_argument("API token lifetime must be positive");

		auto plaintext = generatePlaintext();
		const auto created = now();
		if (lifetimeSeconds > std::numeric_limits<std::int64_t>::max() - created)
			throw std::invalid_argument("API token lifetime is too large");
		ApiToken token(boost::uuids::to_string(boost::uuids::random_generator()()), sha256Hex(plaintext), name, targetType,
			std::move(projects), group, scope, createdBy, created, created + lifetimeSeconds);

		std::lock_guard lock(Mutex);
		// Expired tokens do not count against the limit.
		const auto active = static_cast<std::size_t>(std::count_if(Tokens.begin(), Tokens.end(),
			[&](const auto& entry) { return entry.second.expiresAt() > created; }));
		if (active >= maxCount)
			throw ApiTokenLimitException();
		for (auto iterator = Tokens.begin(); iterator != Tokens.end(); ++iterator) {
			if (iterator->second.name() != name)
				continue;
			if (iterator->second.expiresAt() > created)
				throw ApiTokenNameTakenException(name);
			// Held by an expired token: delete that one (database first) and reuse the name.
			DataBaseController::getInstance()->removeApiToken(iterator->second.id());
			Dirty.erase(iterator->first);
			Tokens.erase(iterator);
			break;
		}

		try {
			DataBaseController::getInstance()->addApiToken(token);
		}
		catch (const mongocxx::operation_exception& e) {
			// (mongocxx 4 has no server_error_category() to compare with, so only the value is checked.)
			// Duplicate key: the unique index on name caught what the check above did not (e.g. another server instance).
			if (e.code().value() == 11000)
				throw ApiTokenNameTakenException(name);
			throw;
		}
		Tokens.insert_or_assign(token.tokenHash(), token);
		return CreatedApiToken{std::move(token), std::move(plaintext)};
	}

	std::optional<ApiToken> ApiTokenStore::validate(const std::string& plaintext)
	{
		// Wrong prefix or length cannot be a token of ours: no hashing, no lookup.
		if (plaintext.size() != TOKEN_LENGTH || plaintext.compare(0, 4, TOKEN_PREFIX) != 0)
			return std::nullopt;

		const auto hash = sha256Hex(plaintext);
		const auto current = now();
		std::lock_guard lock(Mutex);
		const auto iterator = Tokens.find(hash);
		if (iterator == Tokens.end())
			return std::nullopt;
		if (iterator->second.expiresAt() <= current)
			return std::nullopt;

		iterator->second.setLastUsedAt(current);
		Dirty.insert(hash);
		return iterator->second;
	}

	std::vector<ApiToken> ApiTokenStore::list() const
	{
		std::vector<ApiToken> result;
		{
			std::lock_guard lock(Mutex);
			result.reserve(Tokens.size());
			for (const auto& [hash, token] : Tokens)
				result.push_back(token);
		}
		std::sort(result.begin(), result.end(), [](const ApiToken& a, const ApiToken& b) {
			return a.createdAt() != b.createdAt() ? a.createdAt() < b.createdAt() : a.id() < b.id();
		});
		return result;
	}

	std::optional<ApiToken> ApiTokenStore::findById(const std::string& id) const
	{
		std::lock_guard lock(Mutex);
		for (const auto& [hash, token] : Tokens)
			if (token.id() == id)
				return token;
		return std::nullopt;
	}

	bool ApiTokenStore::isExpired(const ApiToken& token) const
	{
		return token.expiresAt() <= now();
	}

	ApiTokenRevokeResult ApiTokenStore::revoke(const std::string& id)
	{
		{
			// Memory first: the token is invalid at once, whatever the database does.
			std::lock_guard lock(Mutex);
			const auto iterator = std::find_if(Tokens.begin(), Tokens.end(), [&](const auto& entry) { return entry.second.id() == id; });
			if (iterator != Tokens.end()) {
				Dirty.erase(iterator->first);
				Tokens.erase(iterator);
				PendingRevocations.insert(id);
			}
			else if (!PendingRevocations.contains(id))
				return ApiTokenRevokeResult::NotFound;
		}

		// The database is called without the mutex. The id stays pending until the delete went through.
		try {
			DataBaseController::getInstance()->removeApiToken(id);
		}
		catch (const std::exception& e) {
			std::cerr << "[error] [api_tokens] revoked token " << id << " could not be deleted from the database, will retry: " << e.what() << std::endl;
			return ApiTokenRevokeResult::RevokedDatabasePending;
		}
		std::lock_guard lock(Mutex);
		PendingRevocations.erase(id);
		return ApiTokenRevokeResult::Revoked;
	}

	std::vector<std::string> ApiTokenStore::pendingRevocations() const
	{
		std::lock_guard lock(Mutex);
		return std::vector<std::string>(PendingRevocations.begin(), PendingRevocations.end());
	}

	void ApiTokenStore::retryPendingRevocations()
	{
		std::vector<std::string> pending;
		{
			std::lock_guard lock(Mutex);
			pending.assign(PendingRevocations.begin(), PendingRevocations.end());
		}
		for (const auto& id : pending) {
			try {
				DataBaseController::getInstance()->removeApiToken(id);
			}
			catch (const std::exception& e) {
				std::cerr << "[error] [api_tokens] revoked token " << id << " still could not be deleted from the database: " << e.what() << std::endl;
				return; // a dead database is not retried in a loop
			}
			std::lock_guard lock(Mutex);
			PendingRevocations.erase(id);
		}
	}

	std::size_t ApiTokenStore::removeProject(const std::string& projectId)
	{
		std::lock_guard lock(Mutex);
		std::size_t affected = 0;
		for (auto iterator = Tokens.begin(); iterator != Tokens.end();) {
			auto& token = iterator->second;
			const auto& projects = token.projects();
			if (token.targetType() != ApiTokenTarget::PROJECT
				|| std::find(projects.begin(), projects.end(), projectId) == projects.end()) {
				++iterator;
				continue;
			}

			std::vector<std::string> remaining;
			for (const auto& project : projects)
				if (project != projectId)
					remaining.push_back(project);

			++affected;
			if (remaining.empty()) {
				DataBaseController::getInstance()->removeApiToken(token.id());
				Dirty.erase(iterator->first);
				iterator = Tokens.erase(iterator);
			}
			else {
				DataBaseController::getInstance()->updateApiTokenProjects(token.id(), remaining);
				token.setProjects(std::move(remaining));
				++iterator;
			}
		}
		return affected;
	}

	void ApiTokenStore::flushLastUsed()
	{
		std::vector<std::pair<std::string, std::pair<std::string, std::int64_t>>> pending; // hash -> id, lastUsedAt
		{
			std::lock_guard lock(Mutex);
			for (const auto& hash : Dirty) {
				const auto iterator = Tokens.find(hash);
				if (iterator != Tokens.end())
					pending.push_back({hash, {iterator->second.id(), iterator->second.lastUsedAt()}});
			}
			Dirty.clear();
		}

		// The database is written without the lock, so requests are not blocked by it. On the first failure everything
		// not yet written is marked dirty again and the next flush tries again; a dead database is not retried in a loop.
		for (std::size_t index = 0; index < pending.size(); ++index) {
			try {
				DataBaseController::getInstance()->updateApiTokenLastUsed(pending[index].second.first, pending[index].second.second);
			}
			catch (const std::exception& e) {
				std::cerr << "[api_tokens] lastUsedAt could not be written: " << e.what() << std::endl;
				std::lock_guard lock(Mutex);
				for (std::size_t rest = index; rest < pending.size(); ++rest)
					if (Tokens.contains(pending[rest].first))
						Dirty.insert(pending[rest].first);
				return;
			}
		}
	}

	std::size_t ApiTokenStore::purgeExpired()
	{
		// Only tokens that expired long ago: a clock that jumped forward must not delete tokens (validate rejects expired ones anyway).
		const auto cutoff = now() - EXPIRED_RETENTION_SECONDS;
		std::size_t removed = 0;
		{
			std::lock_guard lock(Mutex);
			for (auto iterator = Tokens.begin(); iterator != Tokens.end();) {
				if (iterator->second.expiresAt() < cutoff) {
					Dirty.erase(iterator->first);
					iterator = Tokens.erase(iterator);
					++removed;
				}
				else
					++iterator;
			}
		}
		// A database failure is logged and not rethrown: memory is already clean. The documents stay until the next purge.
		try {
			DataBaseController::getInstance()->removeExpiredApiTokens(cutoff);
		}
		catch (const std::exception& e) {
			std::cerr << "[api_tokens] expired tokens could not be deleted from the database: " << e.what() << std::endl;
		}
		return removed;
	}

	std::size_t ApiTokenStore::size() const
	{
		std::lock_guard lock(Mutex);
		return Tokens.size();
	}
}
