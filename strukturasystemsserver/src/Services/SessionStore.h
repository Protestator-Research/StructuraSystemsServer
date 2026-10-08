#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace StructuraSystems::Server
{
	/// Thread safe store of login sessions. Every login gets its own random token.
	/// A session expires after IdleTimeout without use and at the latest MaxLifetime after its creation.
	class SessionStore
	{
	public:
		using Clock = std::chrono::steady_clock;

		static constexpr std::chrono::seconds DEFAULT_IDLE_TIMEOUT = std::chrono::hours(1);
		static constexpr std::chrono::seconds DEFAULT_MAX_LIFETIME = std::chrono::hours(12);

		explicit SessionStore(std::chrono::seconds idleTimeout = DEFAULT_IDLE_TIMEOUT,
			std::chrono::seconds maxLifetime = DEFAULT_MAX_LIFETIME);

		/// Creates a new session and returns its token: 32 random bytes, URL safe base64 without padding.
		std::string createSession(const std::string& username);

		/// Returns the user of a valid session and refreshes its idle timer. Expired sessions are removed.
		std::optional<std::string> validate(const std::string& token);

		void revokeAllFor(const std::string& username);
		/// Revokes all sessions of the user except the one with the given token.
		void revokeAllForExcept(const std::string& username, const std::string& keepToken);

		/// Removes all expired sessions.
		void purgeExpired();

		std::size_t size() const;

	private:
		struct Session {
			std::string Username;
			Clock::time_point Created;
			Clock::time_point LastUsed;
		};

		bool isExpired(const Session& session, Clock::time_point now) const;

		mutable std::mutex Mutex;
		std::unordered_map<std::string, Session> Sessions;
		std::chrono::seconds IdleTimeout;
		std::chrono::seconds MaxLifetime;
	};
}
