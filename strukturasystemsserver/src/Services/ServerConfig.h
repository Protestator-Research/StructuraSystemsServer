#pragma once

#include <chrono>
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

		static ServerConfig& get()
		{
			static ServerConfig config;
			return config;
		}
	};
}
