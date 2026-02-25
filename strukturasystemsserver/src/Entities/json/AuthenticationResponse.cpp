#include "AuthenticationResponse.h"

#include <sysmlv2/rest/entities/JSONEntities.h>

namespace StructuraSystems::Server
{
	AuthenticationResponse::AuthenticationResponse(std::string barrierString)
	{
		BarrierString = barrierString;
	}

	Json::Value AuthenticationResponse::toJson() const
	{
		Json::Value response;
		response["barrierString"] = BarrierString;
		return response;
	}
}
