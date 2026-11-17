#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "../db/ApiToken.hpp"

namespace StructuraSystems::Server {
	/// Thrown when an API token request does not pass validation. Controllers map it to HTTP 400.
	class ApiTokenRequestValidationException : public std::runtime_error {
	public:
		explicit ApiTokenRequestValidationException(const std::string& message) : std::runtime_error(message) {}
	};

	/// Body of POST /admin/tokens. The constructor validates the shape of the request completely; whether the projects exist
	/// is up to the caller. Unknown extra fields are ignored.
	class ApiTokenRequest {
	public:
		static constexpr std::size_t MAX_PROJECTS = 50;

		/// maxExpiresInDays: upper limit of expiresInDays (the configured maximum).
		ApiTokenRequest(const std::string& jsonString, std::int64_t maxExpiresInDays);

		static bool isValidName(const std::string& name);

		const std::string& name() const { return Name; }
		ApiTokenTarget targetType() const { return TargetType; }
		/// Canonical (lower case, hyphenated) UUIDs without duplicates; empty unless the target is a project.
		const std::vector<std::string>& projects() const { return Projects; }
		const std::string& group() const { return Group; }
		ApiTokenScope scope() const { return Scope; }
		const std::optional<std::int64_t>& expiresInDays() const { return ExpiresInDays; }

	private:
		std::string Name;
		ApiTokenTarget TargetType = ApiTokenTarget::SERVER;
		std::vector<std::string> Projects;
		std::string Group;
		ApiTokenScope Scope = ApiTokenScope::READ;
		std::optional<std::int64_t> ExpiresInDays;
	};
} // StructuraSystems::Server
