#include "AuthenticationRequest.h"
#include <sysmlv2/rest/entities/JSONEntities.h>

namespace StructuraSystems::Server
{
	AuthenticationRequest::AuthenticationRequest()
	{

	}

	std::string AuthenticationRequest::username() const
	{
		return Username;
	}

	std::string AuthenticationRequest::password() const
	{
		return Password;
	}
}
