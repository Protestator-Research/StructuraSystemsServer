#include "ApiTokenRequest.h"

#include <algorithm>
#include <regex>
#include <nlohmann/json.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "UserRequest.h"
#include "../../Controller/Helper/RequestHelper.hpp"

namespace StructuraSystems::Server {
	bool ApiTokenRequest::isValidName(const std::string& name) {
		static const std::regex pattern("^[A-Za-z0-9 ._-]{1,64}$");
		return std::regex_match(name, pattern);
	}

	ApiTokenRequest::ApiTokenRequest(const std::string& jsonString, std::int64_t maxExpiresInDays) {
		nlohmann::json json;
		try {
			json = nlohmann::json::parse(jsonString);
		} catch (const nlohmann::json::exception&) {
			throw ApiTokenRequestValidationException("Invalid JSON.");
		}
		if (!json.is_object())
			throw ApiTokenRequestValidationException("Request body must be a JSON object.");

		if (!json.contains("name") || !json["name"].is_string())
			throw ApiTokenRequestValidationException("name is required.");
		Name = json["name"].get<std::string>();
		if (!isValidName(Name))
			throw ApiTokenRequestValidationException("name must match ^[A-Za-z0-9 ._-]{1,64}$.");

		if (!json.contains("target") || !json["target"].is_object())
			throw ApiTokenRequestValidationException("target is required and must be an object.");
		const auto& target = json["target"];
		if (!target.contains("type") || !target["type"].is_string())
			throw ApiTokenRequestValidationException("target.type is required.");
		const auto type = target["type"].get<std::string>();
		if (type != "project" && type != "group" && type != "server")
			throw ApiTokenRequestValidationException("target.type must be project, group or server.");
		TargetType = apiTokenTargetFromString(type);

		const bool hasProjects = target.contains("projects");
		const bool hasGroup = target.contains("group");
		switch (TargetType) {
			case ApiTokenTarget::PROJECT: {
				if (hasGroup)
					throw ApiTokenRequestValidationException("target.group is only allowed for the target type group.");
				if (!hasProjects || !target["projects"].is_array())
					throw ApiTokenRequestValidationException("target.projects is required and must be an array.");
				const auto& list = target["projects"];
				if (list.empty() || list.size() > MAX_PROJECTS)
					throw ApiTokenRequestValidationException("target.projects must contain 1 to 50 project ids.");
				for (const auto& entry : list) {
					if (!entry.is_string())
						throw ApiTokenRequestValidationException("target.projects must contain strings.");
					const auto parsed = parseUuid(entry.get<std::string>());
					if (!parsed)
						throw ApiTokenRequestValidationException("target.projects contains an invalid project id.");
					const auto canonical = boost::uuids::to_string(*parsed);
					if (std::find(Projects.begin(), Projects.end(), canonical) != Projects.end())
						throw ApiTokenRequestValidationException("target.projects contains a duplicate project id.");
					Projects.push_back(canonical);
				}
				break;
			}
			case ApiTokenTarget::GROUP: {
				if (hasProjects)
					throw ApiTokenRequestValidationException("target.projects is only allowed for the target type project.");
				if (!hasGroup || !target["group"].is_string())
					throw ApiTokenRequestValidationException("target.group is required and must be a string.");
				Group = target["group"].get<std::string>();
				if (Group.empty() || !UserRequest::isValidGroup(Group))
					throw ApiTokenRequestValidationException("target.group must match ^[A-Za-z0-9._-]{1,64}$.");
				break;
			}
			case ApiTokenTarget::SERVER:
				if (hasProjects || hasGroup)
					throw ApiTokenRequestValidationException("target.projects and target.group are not allowed for the target type server.");
				break;
		}

		if (!json.contains("scope") || !json["scope"].is_string())
			throw ApiTokenRequestValidationException("scope is required.");
		const auto scope = json["scope"].get<std::string>();
		if (scope != "read" && scope != "read_write")
			throw ApiTokenRequestValidationException("scope must be read or read_write.");
		Scope = apiTokenScopeFromString(scope);

		if (json.contains("expiresInDays")) {
			const auto& days = json["expiresInDays"];
			const auto message = "expiresInDays must be an integer between 1 and " + std::to_string(maxExpiresInDays) + ".";
			if (!days.is_number_integer())
				throw ApiTokenRequestValidationException(message);
			if (days.is_number_unsigned() && days.get<std::uint64_t>() > static_cast<std::uint64_t>(maxExpiresInDays))
				throw ApiTokenRequestValidationException(message);
			const auto value = days.get<std::int64_t>();
			if (value < 1 || value > maxExpiresInDays)
				throw ApiTokenRequestValidationException(message);
			ExpiresInDays = value;
		}
	}
} // StructuraSystems::Server
