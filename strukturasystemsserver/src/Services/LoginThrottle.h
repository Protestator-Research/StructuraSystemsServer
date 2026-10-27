#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace StructuraSystems::Server
{
	/// Limits for the login endpoint, each as a sliding window.
	struct LoginThrottleLimits {
		/// Failed logins per username. Successful logins do not reset the count.
		std::size_t MaxFailuresPerUser = 5;
		std::chrono::seconds FailureWindow = std::chrono::minutes(15);
		/// All login requests per client IP.
		std::size_t MaxRequestsPerIp = 20;
		std::chrono::seconds IpWindow = std::chrono::seconds(60);
	};

	/// Thread safe sliding window rate limiting for /login.
	class LoginThrottle
	{
	public:
		using Clock = std::chrono::steady_clock;

		explicit LoginThrottle(LoginThrottleLimits limits = LoginThrottleLimits());

		/// The throttle of the server, created with the limits from ServerConfig on first use.
		static LoginThrottle& getInstance();

		/// Counts a login request of the client IP and checks both limits, before any password
		/// hash is computed. Returns std::nullopt if the attempt may proceed, otherwise the number
		/// of seconds (at least 1) until the oldest relevant entry expires. A refused request is
		/// not counted again.
		std::optional<std::chrono::seconds> checkAttempt(const std::string& ip, const std::string& username);

		/// Records a failed login (wrong password or unknown user).
		void recordFailure(const std::string& username);

		/// Removes expired entries.
		void purgeExpired();

	private:
		using Window = std::deque<Clock::time_point>;

		static void dropExpired(Window& window, Clock::time_point now, std::chrono::seconds length);
		static std::chrono::seconds retryAfter(const Window& window, Clock::time_point now, std::chrono::seconds length);
		static void purgeMap(std::unordered_map<std::string, Window>& map, Clock::time_point now, std::chrono::seconds length);

		std::mutex Mutex;
		LoginThrottleLimits Limits;
		std::unordered_map<std::string, Window> FailuresByUser;
		std::unordered_map<std::string, Window> RequestsByIp;
	};
}
