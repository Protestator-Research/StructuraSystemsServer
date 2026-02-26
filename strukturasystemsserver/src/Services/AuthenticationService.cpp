#include "AuthenticationService.h"

#include <tuple>
#include <iomanip>
#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string.hpp>

#include <sodium.h>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>

#include "../Controller/DataBaseController.h"

namespace StructuraSystems::Server {
	std::shared_ptr<AuthenticationService> AuthenticationService::Instance = nullptr;

	AuthenticationService::AuthenticationService() {
		DBController = DataBaseController::getInstance();
		readUserFromDatabase();

		if (UsernameHashMap.size()<=0)
			addUser("admin", "admin", USER_ROLE::GLOBAL_ADMINISTRATOR);
	}

	void AuthenticationService::readUserFromDatabase()
	{
		const auto dbRead = DBController->getAllUser();
		for (const auto& element : dbRead)
		{
			const auto [username, securityString] = element;

			UsernameHashMap.insert(std::make_pair(username, securityString));
		}
	}

	std::shared_ptr<AuthenticationService> AuthenticationService::getInstance()
	{
		if (Instance == nullptr) {
			struct conreteAuthService : public AuthenticationService {};
			Instance = std::make_shared<conreteAuthService>();
		}

		return Instance;
	}

	std::string AuthenticationService::authenticateUserWith(const std::string& name, const std::string& password) {

		if (!UsernameHashMap.contains(name))
			throw new std::runtime_error("Username or password wrong.");

		const auto securityString = UsernameHashMap[name].hashedPassword();

		auto check_value = crypto_pwhash_str_verify(
			securityString.c_str(),
			password.c_str(),
			password.size()
		);
		if (check_value==0)
		{
			if (UsernameInstanceMap.contains(name))
			{
				if (UsernameInstanceMap[name]->LastUpdateTime < (std::chrono::system_clock::now() - std::chrono::hours(1))) {
					UsernameInstanceMap[name] = std::make_shared<InstanceStorage>(boost::uuids::random_generator()(),std::chrono::system_clock::now(), name);
					ValidInstances[encode64(boost::uuids::to_string(UsernameInstanceMap[name]->InstanceId))] = UsernameInstanceMap[name];
				}

				UsernameInstanceMap[name]->LastUpdateTime = std::chrono::system_clock::now();
				return encode64(boost::uuids::to_string(UsernameInstanceMap[name]->InstanceId));
			}
			else
			{
				UsernameInstanceMap[name] = std::make_shared<InstanceStorage>(boost::uuids::random_generator()(),std::chrono::system_clock::now(), name);
				ValidInstances[encode64(boost::uuids::to_string(UsernameInstanceMap[name]->InstanceId))] = UsernameInstanceMap[name];
				return encode64(boost::uuids::to_string(UsernameInstanceMap[name]->InstanceId));
			}
		}
		throw new std::runtime_error("Username or password wrong.");
	}

	void AuthenticationService::addUser(std::string username, std::string password, USER_ROLE role)
	{
		char securityString[crypto_pwhash_STRBYTES];
		if (crypto_pwhash_str(
			securityString,
			password.c_str(),
			password.size(),
			crypto_pwhash_OPSLIMIT_INTERACTIVE,
			crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
			std::cerr << "crypto_pwhash_str failed (out of memory?)\n";
			return;
		}
		UsernameHashMap[username] = User(username,std::string(securityString),"", role);
		DBController->addUser(UsernameHashMap[username]);
	}

	bool AuthenticationService::isTokenAuthenticated(std::string bearerToken) {
		if (ValidInstances.contains(bearerToken)) {
			if (ValidInstances[bearerToken]->LastUpdateTime > (std::chrono::system_clock::now() - std::chrono::hours(1))) {
				ValidInstances[bearerToken]->LastUpdateTime = std::chrono::system_clock::now();
				return true;
			}
		}
		return false;
	}

	bool AuthenticationService::isUserAuthenticationAdiministrator(std::string bearerToken) {
		if (isTokenAuthenticated(bearerToken)) {
			std::string username = ValidInstances[bearerToken]->Username;
			if (UsernameHashMap[username].role() == USER_ROLE::GLOBAL_ADMINISTRATOR)
				return true;
		}
		return false;
	}

	User AuthenticationService::getUserFromDatabase(std::string bearerToken) {
		std::string username = ValidInstances[bearerToken]->Username;
		return UsernameHashMap[username];
	}

	std::vector<User> AuthenticationService::getUsersFromDatabase() {
		std::vector<User> users;

		for (const auto& user_pair : UsernameHashMap)
			users.push_back(user_pair.second);

		return users;
	}

	User AuthenticationService::deleteUserFromDatabase(std::string username) {
		User user = UsernameHashMap[username];
		UsernameHashMap.erase(username);
		return user;
	}

	std::string AuthenticationService::decode64(const std::string &value) {
		using namespace boost::archive::iterators;
		using It = transform_width<binary_from_base64<std::string::const_iterator>, 8, 6>;
		return boost::algorithm::trim_right_copy_if(std::string(It(std::begin(value)), It(std::end(value))), [](char c) {
			return c == '\0';
		});
	}

	std::string AuthenticationService::encode64(const std::string &value) {
		using namespace boost::archive::iterators;
		using It = base64_from_binary<transform_width<std::string::const_iterator, 6, 8>>;
		auto tmp = std::string(It(std::begin(value)), It(std::end(value)));
		return tmp.append((3 - value.size() % 3) % 3, '=');
	}
}
