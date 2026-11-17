#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "LoginThrottle.h"
#include "SessionStore.h"

namespace StructuraSystems::Server
{
	/// Runtime settings that main() reads from the config file before the services are created.
	/// Set the values before the first service is used, they are not synchronised.
	struct ServerConfig {
		/// Addresses of reverse proxies whose X-Forwarded-For header is trusted.
		std::vector<std::string> TrustedProxies;
		std::chrono::seconds SessionIdleTimeout = SessionStore::DEFAULT_IDLE_TIMEOUT;
		std::chrono::seconds SessionMaxLifetime = SessionStore::DEFAULT_MAX_LIFETIME;
		LoginThrottleLimits LoginLimits;
		/// API tokens (config keys api_token_*): lifetime in days if the request has none, upper limit of the lifetime
		/// (at most 365, main() caps larger values) and the highest number of tokens that exist at the same time.
		std::int64_t ApiTokenDefaultLifetimeDays = 90;
		std::int64_t ApiTokenMaxLifetimeDays = 365;
		std::size_t ApiTokenMaxCount = 200;
		/// TEST ONLY (config key api_token_lifetime_override_seconds): if greater than 0, a new API token lives at most this many
		/// seconds (the minimum of the requested days and this value), so tests can issue tokens that expire quickly. 0 = off, never set it in production.
		std::int64_t ApiTokenLifetimeOverrideSeconds = 0;

		static ServerConfig& get()
		{
			static ServerConfig config;
			return config;
		}
	};
}
