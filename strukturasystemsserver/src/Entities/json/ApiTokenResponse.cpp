#include "ApiTokenResponse.h"

#include <nlohmann/json.hpp>

namespace StructuraSystems::Server {
	ApiTokenResponse::ApiTokenResponse(const ApiToken& token, bool expired, const std::string& plaintext)
		: Id(token.id()), Name(token.name()), TargetType(token.targetType()), Projects(token.projects()), Group(token.group()),
		Scope(token.scope()), CreatedBy(token.createdBy()), CreatedAt(token.createdAt()), ExpiresAt(token.expiresAt()),
		LastUsedAt(token.lastUsedAt()), Expired(expired), Plaintext(plaintext) {}

	std::string ApiTokenResponse::serializeToJson() const {
		nlohmann::json target;
		target["type"] = toString(TargetType);
		if (TargetType == ApiTokenTarget::PROJECT)
			target["projects"] = Projects;
		else if (TargetType == ApiTokenTarget::GROUP)
			target["group"] = Group;

		nlohmann::json json;
		json["id"] = Id;
		json["name"] = Name;
		json["target"] = target;
		json["scope"] = toString(Scope);
		json["createdBy"] = CreatedBy;
		// All times are unix seconds; lastUsedAt 0 means the token was never used.
		json["createdAt"] = CreatedAt;
		json["expiresAt"] = ExpiresAt;
		json["lastUsedAt"] = LastUsedAt;
		// Expired tokens stay visible until they are purged; they no longer authenticate.
		json["expired"] = Expired;
		if (!Plaintext.empty())
			json["token"] = Plaintext;
		return json.dump();
	}
} // StructuraSystems::Server
