#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

namespace StructuraSystems::Server
{
	enum class ApiTokenTarget {
		PROJECT,
		GROUP,
		SERVER
	};

	enum class ApiTokenScope {
		READ,
		READ_WRITE
	};

	inline std::string toString(ApiTokenTarget target)
	{
		switch (target) {
			case ApiTokenTarget::PROJECT: return "project";
			case ApiTokenTarget::GROUP: return "group";
			case ApiTokenTarget::SERVER: return "server";
		}
		throw std::invalid_argument("Unknown API token target");
	}

	inline ApiTokenTarget apiTokenTargetFromString(const std::string& value)
	{
		if (value == "project") return ApiTokenTarget::PROJECT;
		if (value == "group") return ApiTokenTarget::GROUP;
		if (value == "server") return ApiTokenTarget::SERVER;
		throw std::invalid_argument("Unknown API token target: " + value);
	}

	inline std::string toString(ApiTokenScope scope)
	{
		switch (scope) {
			case ApiTokenScope::READ: return "read";
			case ApiTokenScope::READ_WRITE: return "read_write";
		}
		throw std::invalid_argument("Unknown API token scope");
	}

	inline ApiTokenScope apiTokenScopeFromString(const std::string& value)
	{
		if (value == "read") return ApiTokenScope::READ;
		if (value == "read_write") return ApiTokenScope::READ_WRITE;
		throw std::invalid_argument("Unknown API token scope: " + value);
	}

	/// Metadata of an API token. Only the SHA-256 hash of the token is a member, the plaintext exists only
	/// once, in the result of ApiTokenStore::create. All times are unix seconds, lastUsedAt 0 means never used.
	class ApiToken {
	public:
		/// Throws std::invalid_argument unless exactly the field of the target type is set: projects (not empty) for
		/// PROJECT, group (not empty) for GROUP, neither for SERVER.
		static void validateTarget(ApiTokenTarget targetType, const std::vector<std::string>& projects, const std::string& group)
		{
			switch (targetType) {
				case ApiTokenTarget::PROJECT:
					if (projects.empty() || !group.empty())
						throw std::invalid_argument("A project token needs projects and no group");
					return;
				case ApiTokenTarget::GROUP:
					if (group.empty() || !projects.empty())
						throw std::invalid_argument("A group token needs a group and no projects");
					return;
				case ApiTokenTarget::SERVER:
					if (!group.empty() || !projects.empty())
						throw std::invalid_argument("A server token has neither projects nor a group");
					return;
			}
			throw std::invalid_argument("Unknown API token target");
		}

		ApiToken(std::string id, std::string tokenHash, std::string name, ApiTokenTarget targetType,
			std::vector<std::string> projects, std::string group, ApiTokenScope scope, std::string createdBy,
			std::int64_t createdAt, std::int64_t expiresAt, std::int64_t lastUsedAt = 0)
			: Id(std::move(id)), TokenHash(std::move(tokenHash)), Name(std::move(name)), TargetType(targetType),
			Projects(std::move(projects)), Group(std::move(group)), Scope(scope), CreatedBy(std::move(createdBy)),
			CreatedAt(createdAt), ExpiresAt(expiresAt), LastUsedAt(lastUsedAt)
		{
			validateTarget(TargetType, Projects, Group);
		}

		/// Parses a database document. Throws on missing or invalid fields.
		explicit ApiToken(const std::string& jsonString)
		{
			const auto json = nlohmann::json::parse(jsonString);
			Id = json.at("_id").get<std::string>();
			TokenHash = json.at("tokenHash").get<std::string>();
			Name = json.at("name").get<std::string>();
			TargetType = apiTokenTargetFromString(json.at("targetType").get<std::string>());
			if (json.contains("projects") && json["projects"].is_array())
				Projects = json["projects"].get<std::vector<std::string>>();
			if (json.contains("group") && json["group"].is_string())
				Group = json["group"].get<std::string>();
			Scope = apiTokenScopeFromString(json.at("scope").get<std::string>());
			validateTarget(TargetType, Projects, Group);
			CreatedBy = json.at("createdBy").get<std::string>();
			CreatedAt = json.at("createdAt").get<std::int64_t>();
			ExpiresAt = json.at("expiresAt").get<std::int64_t>();
			if (json.contains("lastUsedAt") && json["lastUsedAt"].is_number_integer())
				LastUsedAt = json["lastUsedAt"].get<std::int64_t>();
		}

		const std::string& id() const { return Id; }
		const std::string& tokenHash() const { return TokenHash; }
		const std::string& name() const { return Name; }
		ApiTokenTarget targetType() const { return TargetType; }
		const std::vector<std::string>& projects() const { return Projects; }
		const std::string& group() const { return Group; }
		ApiTokenScope scope() const { return Scope; }
		const std::string& createdBy() const { return CreatedBy; }
		std::int64_t createdAt() const { return CreatedAt; }
		std::int64_t expiresAt() const { return ExpiresAt; }
		std::int64_t lastUsedAt() const { return LastUsedAt; }

		/// Throws std::invalid_argument for an empty list: a token without projects is deleted, not emptied.
		void setProjects(std::vector<std::string> projects)
		{
			if (projects.empty())
				throw std::invalid_argument("An API token needs at least one project");
			Projects = std::move(projects);
		}
		void setLastUsedAt(std::int64_t lastUsedAt) { LastUsedAt = lastUsedAt; }

	private:
		std::string Id;
		std::string TokenHash;
		std::string Name;
		ApiTokenTarget TargetType = ApiTokenTarget::PROJECT;
		std::vector<std::string> Projects;
		std::string Group;
		ApiTokenScope Scope = ApiTokenScope::READ;
		std::string CreatedBy;
		std::int64_t CreatedAt = 0;
		std::int64_t ExpiresAt = 0;
		std::int64_t LastUsedAt = 0;
	};
}
