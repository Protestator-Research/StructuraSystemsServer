#pragma once

#include <memory>
#include <optional>
#include <string>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <json/json.h>

namespace StructuraSystems::Server
{
	/// Parses a UUID from a string. Returns std::nullopt for invalid input instead of throwing.
	inline std::optional<boost::uuids::uuid> parseUuid(const std::string& value)
	{
		try {
			return boost::uuids::string_generator()(value);
		}
		catch (...) {
			return std::nullopt;
		}
	}

	/// Returns the JSON body of the request or nullptr if there is none or it is invalid.
	inline std::shared_ptr<Json::Value> requireJson(const drogon::HttpRequestPtr& request)
	{
		return request->getJsonObject();
	}

	/// Creates a JSON error response of the form {"error": message}.
	inline drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode code, const std::string& message)
	{
		Json::Value body(Json::objectValue);
		body["error"] = message;
		auto response = drogon::HttpResponse::newHttpJsonResponse(body);
		response->setStatusCode(code);
		return response;
	}
}
