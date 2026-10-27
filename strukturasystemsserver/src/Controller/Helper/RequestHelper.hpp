#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <algorithm>
#include <exception>
#include <iostream>
#include <cctype>
#include <functional>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/string_generator.hpp>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include <drogon/utils/Utilities.h>
#include <json/json.h>
#include <sysmlv2/rest/entities/ChangeType.h>

#include "../../Services/ServerConfig.h"

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

	/// All values of a query parameter, in the order of the request (a parameter can be repeated). The values are decoded.
	inline std::vector<std::string> queryValues(const drogon::HttpRequestPtr& request, const std::string& name)
	{
		std::vector<std::string> values;
		const std::string query = request->query();
		size_t position = 0;
		while (position <= query.size())
		{
			auto end = query.find('&', position);
			if (end == std::string::npos)
				end = query.size();
			const auto pair = query.substr(position, end - position);
			const auto equals = pair.find('=');
			const auto key = drogon::utils::urlDecode(pair.substr(0, equals));
			if (key == name)
				values.push_back(equals == std::string::npos ? std::string() : drogon::utils::urlDecode(pair.substr(equals + 1)));
			position = end + 1;
		}
		return values;
	}

	/// Reads the change types of a request: the parameter changeTypes (comma separated and/or repeated) or its alias
	/// change_type. No parameter, or only empty values, means no filter (all changes). Returns false for an unknown value.
	inline bool parseChangeTypes(const drogon::HttpRequestPtr& request, std::vector<int>& changeTypes)
	{
		changeTypes.clear();
		for (const char* name : {"changeTypes", "change_type"})
		{
			for (const auto& value : queryValues(request, name))
			{
				size_t position = 0;
				while (position <= value.size())
				{
					auto end = value.find(',', position);
					if (end == std::string::npos)
						end = value.size();
					auto token = value.substr(position, end - position);
					position = end + 1;

					const auto first = token.find_first_not_of(" \t");
					if (first == std::string::npos)
						continue;
					token = token.substr(first, token.find_last_not_of(" \t") - first + 1);
					try {
						const int type = SysMLv2::REST::changeTypeFromString(token);
						if (std::find(changeTypes.begin(), changeTypes.end(), type) == changeTypes.end())
							changeTypes.push_back(type);
					}
					catch (const std::invalid_argument&) {
						return false;
					}
				}
			}
		}
		return true;
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

	/// The answer for an exception that the route does not expect (database failure, broken history, ...): a 500 with a JSON
	/// body. The reason is logged and not sent to the client.
	inline drogon::HttpResponsePtr internalError(const std::exception& exception);

	/// Creates a JSON error response of the form {"error": message}.
	inline drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode code, const std::string& message)
	{
		Json::Value body(Json::objectValue);
		body["error"] = message;
		auto response = drogon::HttpResponse::newHttpJsonResponse(body);
		response->setStatusCode(code);
		return response;
	}

	inline drogon::HttpResponsePtr internalError(const std::exception& exception)
	{
		std::cerr << "[error] request failed: " << exception.what() << std::endl;
		return jsonError(drogon::k500InternalServerError, "Internal server error.");
	}

	// -----------------------------------------------------------------------------------------------------------------
	// Pagination (SysML v2 API and Services, REST/HTTP PSM)
	// -----------------------------------------------------------------------------------------------------------------

	inline constexpr size_t DEFAULT_PAGE_SIZE = 100;
	inline constexpr size_t MAX_PAGE_SIZE = 1000;

	/// The first value of a query parameter, std::nullopt if the parameter is not there.
	inline std::optional<std::string> firstQueryValue(const drogon::HttpRequestPtr& request, const std::string& name)
	{
		const auto values = queryValues(request, name);
		if (values.empty())
			return std::nullopt;
		return values.front();
	}

	/// The scheme and the authority (host[:port]) the client used. Behind a reverse proxy X-Forwarded-Proto and
	/// X-Forwarded-Host are only believed if the direct peer is one of the configured trusted proxies, like X-Forwarded-For.
	inline std::string externalOrigin(const drogon::HttpRequestPtr& request)
	{
		const auto& trusted = ServerConfig::get().TrustedProxies;
		const bool fromProxy = std::find(trusted.begin(), trusted.end(), request->peerAddr().toIp()) != trusted.end();

		const auto firstOf = [](std::string value) {
			value = value.substr(0, value.find(','));
			const auto begin = value.find_first_not_of(" \t");
			if (begin == std::string::npos)
				return std::string();
			return value.substr(begin, value.find_last_not_of(" \t") - begin + 1);
		};
		const auto validHost = [](const std::string& host) {
			return !host.empty() && host.size() <= 255 && std::all_of(host.begin(), host.end(), [](unsigned char c) {
				return std::isalnum(c) || c == '.' || c == '-' || c == ':' || c == '[' || c == ']' || c == '_';
			});
		};

		std::string scheme = "http";
		std::string host = request->getHeader("Host");
		if (fromProxy)
		{
			const auto proto = firstOf(request->getHeader("X-Forwarded-Proto"));
			if (proto == "http" || proto == "https")
				scheme = proto;
			const auto forwardedHost = firstOf(request->getHeader("X-Forwarded-Host"));
			if (validHost(forwardedHost))
				host = forwardedHost;
		}
		if (!validHost(host))
			host = "localhost";
		return scheme + "://" + host;
	}

	/// The URL of the request with the paging cursor replaced: all other query parameters stay as they were sent.
	inline std::string pageUrl(const drogon::HttpRequestPtr& request, const std::string& cursorName, const std::string& cursor)
	{
		std::string url = externalOrigin(request) + request->path() + "?";
		const std::string query = request->query();
		size_t position = 0;
		while (position < query.size())
		{
			auto end = query.find('&', position);
			if (end == std::string::npos)
				end = query.size();
			const auto pair = query.substr(position, end - position);
			position = end + 1;
			if (pair.empty())
				continue;
			const auto key = drogon::utils::urlDecode(pair.substr(0, pair.find('=')));
			if (key == "page[after]" || key == "page[before]")
				continue;
			url += pair + "&";
		}
		return url + drogon::utils::urlEncodeComponent(cursorName) + "=" + cursor;
	}

	/// An opaque cursor for the key of an item (base64url, without padding).
	inline std::string encodeCursor(const std::string& key)
	{
		return drogon::utils::base64Encode(reinterpret_cast<const unsigned char*>(key.data()), key.size(), true, false);
	}

	/// The answer for a collection: a JSON array with the page that page[size], page[after] and page[before] select, and a
	/// Link header (RFC 8288) with rel="next" and rel="prev" if there are further pages. The cursors are the keys of the items
	/// in the order of the list, so the pages stay stable while the list does not change. An invalid page[size], an unknown or
	/// broken cursor, or both cursors at once answer 400.
	/// @param items The whole collection, in a stable order.
	/// @param keyOf Item -> string. Unique within the collection (an id).
	/// @param toJson Item -> JSON text of the item. Only called for the items of the page.
	template<typename Item, typename KeyOf, typename ToJson>
	inline drogon::HttpResponsePtr paginate(const drogon::HttpRequestPtr& request, const std::vector<Item>& items, KeyOf keyOf, ToJson toJson)
	{
		size_t size = DEFAULT_PAGE_SIZE;
		if (const auto sizeValue = firstQueryValue(request, "page[size]"))
		{
			const auto& text = *sizeValue;
			if (text.empty() || text.size() > 6 || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
				return jsonError(drogon::k400BadRequest, "page[size] must be a number between 1 and 1000.");
			size = std::stoul(text);
			if (size < 1 || size > MAX_PAGE_SIZE)
				return jsonError(drogon::k400BadRequest, "page[size] must be a number between 1 and 1000.");
		}

		const auto after = firstQueryValue(request, "page[after]");
		const auto before = firstQueryValue(request, "page[before]");
		if (after && before)
			return jsonError(drogon::k400BadRequest, "page[after] and page[before] can not be used together.");

		// The position of the cursor in the list.
		const auto positionOf = [&](const std::string& cursor) -> std::optional<size_t> {
			if (cursor.empty() || cursor.size() > 2048)
				return std::nullopt;
			if (!std::all_of(cursor.begin(), cursor.end(), [](unsigned char c) { return std::isalnum(c) || c == '-' || c == '_'; }))
				return std::nullopt;
			const auto key = drogon::utils::base64Decode(cursor);
			// only the cursors that were handed out are valid, not other spellings of them
			if (encodeCursor(key) != cursor)
				return std::nullopt;
			for (size_t index = 0; index < items.size(); index++)
			{
				if (keyOf(items[index]) == key)
					return index;
			}
			return std::nullopt;
		};

		size_t begin = 0;
		size_t end = std::min(size, items.size());
		if (after)
		{
			const auto position = positionOf(*after);
			if (!position)
				return jsonError(drogon::k400BadRequest, "Unknown or invalid page[after] cursor.");
			begin = *position + 1;
			end = std::min(begin + size, items.size());
		}
		else if (before)
		{
			const auto position = positionOf(*before);
			if (!position)
				return jsonError(drogon::k400BadRequest, "Unknown or invalid page[before] cursor.");
			end = *position;
			begin = end > size ? end - size : 0;
		}

		std::string body = "[\r\n";
		for (size_t index = begin; index < end; index++)
		{
			body += toJson(items[index]);
			if (index + 1 < end)
				body += ",\r\n";
		}
		body += "]";

		const auto response = drogon::HttpResponse::newHttpResponse(drogon::k200OK, drogon::CT_APPLICATION_JSON);
		response->setBody(body);

		std::string link;
		if (end < items.size() && begin < end)
			link += "<" + pageUrl(request, "page[after]", encodeCursor(keyOf(items[end - 1]))) + ">; rel=\"next\"";
		if (begin > 0 && begin < items.size())
		{
			if (!link.empty())
				link += ", ";
			link += "<" + pageUrl(request, "page[before]", encodeCursor(keyOf(items[begin]))) + ">; rel=\"prev\"";
		}
		if (!link.empty())
			response->addHeader("Link", link);
		return response;
	}
}
