#include "LoginThrottle.h"

#include "ServerConfig.h"

namespace StructuraSystems::Server
{
	namespace
	{
		// Upper bound for tracked keys so that random usernames or addresses cannot exhaust memory.
		constexpr std::size_t MAX_TRACKED_KEYS = 100000;
		constexpr std::size_t MAX_KEY_LENGTH = 256;
	}

	LoginThrottle::LoginThrottle(LoginThrottleLimits limits)
		: Limits(limits)
	{
	}

	LoginThrottle& LoginThrottle::getInstance()
	{
		// A function-local static is initialised exactly once, even with concurrent first calls.
		static LoginThrottle instance(ServerConfig::get().LoginLimits);
		return instance;
	}

	void LoginThrottle::dropExpired(Window& window, Clock::time_point now, std::chrono::seconds length)
	{
		while (!window.empty() && now - window.front() >= length)
			window.pop_front();
	}

	std::chrono::seconds LoginThrottle::retryAfter(const Window& window, Clock::time_point now, std::chrono::seconds length)
	{
		const auto remaining = window.front() + length - now;
		const auto seconds = std::chrono::ceil<std::chrono::seconds>(remaining);
		return seconds < std::chrono::seconds(1) ? std::chrono::seconds(1) : seconds;
	}

	std::optional<std::chrono::seconds> LoginThrottle::checkAttempt(const std::string& ip, const std::string& username)
	{
		const auto now = Clock::now();
		const auto ipKey = ip.substr(0, MAX_KEY_LENGTH);
		const auto userKey = username.substr(0, MAX_KEY_LENGTH);

		std::lock_guard lock(Mutex);

		if (RequestsByIp.size() < MAX_TRACKED_KEYS || RequestsByIp.contains(ipKey)) {
			auto& requests = RequestsByIp[ipKey];
			dropExpired(requests, now, Limits.IpWindow);
			if (requests.size() >= Limits.MaxRequestsPerIp)
				return retryAfter(requests, now, Limits.IpWindow);
			requests.push_back(now);
		}

		const auto failures = FailuresByUser.find(userKey);
		if (failures != FailuresByUser.end()) {
			dropExpired(failures->second, now, Limits.FailureWindow);
			if (failures->second.size() >= Limits.MaxFailuresPerUser)
				return retryAfter(failures->second, now, Limits.FailureWindow);
		}
		return std::nullopt;
	}

	void LoginThrottle::recordFailure(const std::string& username)
	{
		const auto now = Clock::now();
		const auto userKey = username.substr(0, MAX_KEY_LENGTH);

		std::lock_guard lock(Mutex);
		if (FailuresByUser.size() >= MAX_TRACKED_KEYS && !FailuresByUser.contains(userKey))
			return;
		auto& failures = FailuresByUser[userKey];
		dropExpired(failures, now, Limits.FailureWindow);
		failures.push_back(now);
	}

	void LoginThrottle::purgeMap(std::unordered_map<std::string, Window>& map, Clock::time_point now, std::chrono::seconds length)
	{
		for (auto iterator = map.begin(); iterator != map.end();) {
			dropExpired(iterator->second, now, length);
			if (iterator->second.empty())
				iterator = map.erase(iterator);
			else
				++iterator;
		}
	}

	void LoginThrottle::purgeExpired()
	{
		const auto now = Clock::now();
		std::lock_guard lock(Mutex);
		purgeMap(FailuresByUser, now, Limits.FailureWindow);
		purgeMap(RequestsByIp, now, Limits.IpWindow);
	}
}
