#pragma once

#include <string>
#include <json/value.h>

namespace StructuraSystems::Server
{
	class AuthenticationResponse
	{
	public:
		AuthenticationResponse() = delete;
		AuthenticationResponse(std::string barrierString);
		~AuthenticationResponse() = default;

		Json::Value toJson() const;

	private:
		std::string BarrierString;
	};
}
