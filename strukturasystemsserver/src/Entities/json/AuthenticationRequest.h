#pragma once

#include <string>

namespace StructuraSystems::Server
{
	class AuthenticationRequest
	{
	public:
		AuthenticationRequest();
		~AuthenticationRequest() = default;


		std::string username() const;
		std::string password() const;
	private:
		std::string Username;
		std::string Password;
	};
}
