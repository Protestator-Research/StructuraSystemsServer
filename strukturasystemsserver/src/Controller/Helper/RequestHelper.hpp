#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <algorithm>
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

	/// Returns the address of the client. The first entry of X-Forwarded-For is only used if the
	/// direct peer is one of the trusted reverse proxies, otherwise the header could be spoofed.
	inline std::string clientIp(const drogon::HttpRequestPtr& request, const std::vector<std::string>& trustedProxies)
	{
		const auto peer = request->peerAddr().toIp();
		if (std::find(trustedProxies.begin(), trustedProxies.end(), peer) == trustedProxies.end())
			return peer;

		const auto& forwarded = request->getHeader("X-Forwarded-For");
		auto first = forwarded.substr(0, forwarded.find(','));
		const auto begin = first.find_first_not_of(" \t");
		if (begin == std::string::npos)
			return peer;
		first = first.substr(begin, first.find_last_not_of(" \t") - begin + 1);
		// An IP address has at most 45 characters (IPv6 with an embedded IPv4 address).
		if (first.size() > 45)
			return peer;
		return first;
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
