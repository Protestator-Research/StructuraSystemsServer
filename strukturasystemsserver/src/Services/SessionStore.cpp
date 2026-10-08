#include "SessionStore.h"

#include <sodium.h>

#include <array>

namespace StructuraSystems::Server
{
	namespace
	{
		constexpr std::size_t TOKEN_BYTES = 32;
		// A valid token has 43 characters. Anything much longer is rejected without a lookup.
		constexpr std::size_t MAX_TOKEN_LENGTH = 128;
	}

	SessionStore::SessionStore(std::chrono::seconds idleTimeout, std::chrono::seconds maxLifetime)
		: IdleTimeout(idleTimeout), MaxLifetime(maxLifetime)
	{
	}

	std::string SessionStore::createSession(const std::string& username)
	{
		std::array<unsigned char, TOKEN_BYTES> raw{};
		randombytes_buf(raw.data(), raw.size());

		std::string token(sodium_base64_ENCODED_LEN(raw.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING), '\0');
		sodium_bin2base64(token.data(), token.size(), raw.data(), raw.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
		// sodium_bin2base64 writes a terminating NUL that is part of the computed length.
		token.resize(token.find('\0'));
		sodium_memzero(raw.data(), raw.size());

		const auto now = Clock::now();
		std::lock_guard lock(Mutex);
		Sessions[token] = Session{username, now, now};
		return token;
	}

	bool SessionStore::isExpired(const Session& session, Clock::time_point now) const
	{
		return now - session.LastUsed >= IdleTimeout || now - session.Created >= MaxLifetime;
	}

	std::optional<std::string> SessionStore::validate(const std::string& token)
	{
		if (token.empty() || token.size() > MAX_TOKEN_LENGTH)
			return std::nullopt;

		const auto now = Clock::now();
		std::lock_guard lock(Mutex);
		const auto iterator = Sessions.find(token);
		if (iterator == Sessions.end())
			return std::nullopt;

		if (isExpired(iterator->second, now)) {
			Sessions.erase(iterator);
			return std::nullopt;
		}

		iterator->second.LastUsed = now;
		return iterator->second.Username;
	}

	void SessionStore::revokeAllFor(const std::string& username)
	{
		std::lock_guard lock(Mutex);
		for (auto iterator = Sessions.begin(); iterator != Sessions.end();) {
			if (iterator->second.Username == username)
				iterator = Sessions.erase(iterator);
			else
				++iterator;
		}
	}

	void SessionStore::revokeAllForExcept(const std::string& username, const std::string& keepToken)
	{
		std::lock_guard lock(Mutex);
		for (auto iterator = Sessions.begin(); iterator != Sessions.end();) {
			if (iterator->second.Username == username && iterator->first != keepToken)
				iterator = Sessions.erase(iterator);
			else
				++iterator;
		}
	}

	void SessionStore::purgeExpired()
	{
		const auto now = Clock::now();
		std::lock_guard lock(Mutex);
		for (auto iterator = Sessions.begin(); iterator != Sessions.end();) {
			if (isExpired(iterator->second, now))
				iterator = Sessions.erase(iterator);
			else
				++iterator;
		}
	}

	std::size_t SessionStore::size() const
	{
		std::lock_guard lock(Mutex);
		return Sessions.size();
	}
}
