
#pragma once

#include <string>
#include <map>
#include <boost/uuid/uuid.hpp>
#include <drogon/HttpFilter.h>
#include "../Entities/db/User.hpp"

namespace StructuraSystems::Server
{
	class DataBaseController;

	struct InstanceStorage {
		boost::uuids::uuid InstanceId;
		std::chrono::time_point<std::chrono::system_clock> LastUpdateTime;
		std::string Username;
	};

	class AuthenticationService
	{
	public:
		virtual ~AuthenticationService() = default;

		static std::shared_ptr<AuthenticationService> getInstance();

		std::string authenticateUserWith(const std::string& name, const std::string& password);

		void addUser(std::string username, std::string password, USER_ROLE role = USER_ROLE::USER);

		bool isTokenAuthenticated(std::string bearerToken);
		bool isUserAuthenticationAdiministrator(std::string bearerToken);
	private:
		AuthenticationService();

		std::string decode64(const std::string& value);
		std::string encode64(const std::string& value);

		static std::shared_ptr<AuthenticationService> Instance;

		void readUserFromDatabase();

		std::map<std::string, User> UsernameHashMap;
		std::map<std::string, std::shared_ptr<InstanceStorage>> UsernameInstanceMap;
		std::map<std::string, std::shared_ptr<InstanceStorage>> ValidInstances;

		DataBaseController* DBController;
	};

}