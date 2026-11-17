#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../db/ApiToken.hpp"

namespace StructuraSystems::Server {
	/// JSON of an API token for the admin routes. Never contains the hash. The plaintext is only given for the answer to the
	/// creation (POST /admin/tokens) and appears as "token".
	class ApiTokenResponse {
	public:
		ApiTokenResponse(const ApiToken& token, bool expired, const std::string& plaintext = "");

		std::string serializeToJson() const;

	private:
		std::string Id;
		std::string Name;
		ApiTokenTarget TargetType;
		std::vector<std::string> Projects;
		std::string Group;
		ApiTokenScope Scope;
		std::string CreatedBy;
		std::int64_t CreatedAt;
		std::int64_t ExpiresAt;
		std::int64_t LastUsedAt;
		bool Expired;
		std::string Plaintext;
	};
} // StructuraSystems::Server
