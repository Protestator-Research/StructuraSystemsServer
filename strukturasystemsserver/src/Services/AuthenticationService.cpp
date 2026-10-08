#include "AuthenticationService.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <tuple>
#include <iomanip>
#include <sodium.h>

#include <boost/uuid/nil_generator.hpp>
#include <boost/uuid/random_generator.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/uuid/string_generator.hpp>
#include <boost/uuid/uuid_io.hpp>


#include "../Controller/DataBaseController.h"

namespace StructuraSystems::Server {
	std::shared_ptr<AuthenticationService> AuthenticationService::Instance = nullptr;

	AuthenticationService::AuthenticationService() {
		DBController = DataBaseController::getInstance();
		readUserFromDatabase();

		if (UsernameHashMap.empty())
			createInitialAdministrator();
	}

	void AuthenticationService::createInitialAdministrator()
	{
		const char* envPassword = std::getenv("STRUCTURA_ADMIN_PASSWORD");
		if (envPassword != nullptr && envPassword[0] != '\0') {
			if (std::strlen(envPassword) < 12)
				std::cerr << "[auth] Warning: STRUCTURA_ADMIN_PASSWORD has fewer than 12 characters." << std::endl;
			addUser("admin", envPassword, USER_ROLE::GLOBAL_ADMINISTRATOR);
			std::cout << "[auth] Initial administrator 'admin' created with the password from STRUCTURA_ADMIN_PASSWORD." << std::endl;
			return;
		}

		// Unambiguous alphabet: no 0/O, 1/l/I.
		static const std::string alphabet = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
		std::string password;
		for (int i = 0; i < 24; ++i)
			password += alphabet[randombytes_uniform(static_cast<uint32_t>(alphabet.size()))];

		addUser("admin", password, USER_ROLE::GLOBAL_ADMINISTRATOR, "", true);
		std::cout << "[auth] ================================================================\n"
		          << "[auth] Initial administrator created (shown only once, change it now):\n"
		          << "[auth] Username: admin\n"
		          << "[auth] Password: " << password << "\n"
		          << "[auth] ================================================================" << std::endl;
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

		const auto userIterator = UsernameHashMap.find(name);
		if (userIterator == UsernameHashMap.end())
			throw std::runtime_error("Username or password wrong.");

		const auto securityString = userIterator->second.hashedPassword();

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
		throw std::runtime_error("Username or password wrong.");
	}

	std::string AuthenticationService::hashPassword(const std::string& password)
	{
		char securityString[crypto_pwhash_STRBYTES];
		if (crypto_pwhash_str(
			securityString,
			password.c_str(),
			password.size(),
			crypto_pwhash_OPSLIMIT_INTERACTIVE,
			crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
			throw std::runtime_error("crypto_pwhash_str failed (out of memory?)");
		return std::string(securityString);
	}

	void AuthenticationService::invalidateTokens(const std::string& username)
	{
		UsernameInstanceMap.erase(username);
		for (auto iterator = ValidInstances.begin(); iterator != ValidInstances.end();) {
			if (iterator->second->Username == username)
				iterator = ValidInstances.erase(iterator);
			else
				++iterator;
		}
	}

	void AuthenticationService::addUser(std::string username, std::string password, USER_ROLE role, std::string group, bool mustChangePassword)
	{
		if (UsernameHashMap.contains(username))
			throw UserAlreadyExistsException(username);

		User user(username, hashPassword(password), std::move(group), role);
		user.setMustChangePassword(mustChangePassword);

		DBController->addUser(user);
		UsernameHashMap[username] = user;
	}

	bool AuthenticationService::isTokenAuthenticated(std::string bearerToken) {
		const auto instance = ValidInstances.find(bearerToken);
		if (instance == ValidInstances.end())
			return false;
		if (!UsernameHashMap.contains(instance->second->Username))
			return false;
		if (instance->second->LastUpdateTime > (std::chrono::system_clock::now() - std::chrono::hours(1))) {
			instance->second->LastUpdateTime = std::chrono::system_clock::now();
			return true;
		}
		return false;
	}

	bool AuthenticationService::isUserAuthenticationAdministrator(std::string bearerToken) {
		const auto user = getUserFromDatabase(bearerToken);
		return user.has_value() && user->role() == USER_ROLE::GLOBAL_ADMINISTRATOR;
	}

	std::optional<User> AuthenticationService::getUserFromDatabase(std::string bearerToken) {
		const auto instance = ValidInstances.find(bearerToken);
		if (instance == ValidInstances.end())
			return std::nullopt;
		return findUser(instance->second->Username);
	}

	std::optional<User> AuthenticationService::findUser(const std::string& username) {
		const auto user = UsernameHashMap.find(username);
		if (user == UsernameHashMap.end())
			return std::nullopt;
		return user->second;
	}

	std::vector<User> AuthenticationService::getUsersFromDatabase() {
		std::vector<User> users;

		for (const auto& user_pair : UsernameHashMap)
			users.push_back(user_pair.second);

		return users;
	}

	size_t AuthenticationService::countAdministrators() {
		size_t count = 0;
		for (const auto& user_pair : UsernameHashMap)
			if (user_pair.second.role() == USER_ROLE::GLOBAL_ADMINISTRATOR)
				++count;
		return count;
	}

	User AuthenticationService::deleteUserFromDatabase(std::string username) {
		const auto iterator = UsernameHashMap.find(username);
		if (iterator == UsernameHashMap.end())
			throw UserNotFoundException(username);

		User user = iterator->second;
		DBController->removeUser(username);
		UsernameHashMap.erase(username);
		invalidateTokens(username);

		return user;
	}

	User AuthenticationService::changeUserInDatabase(std::string username, std::optional<std::string> password, std::optional<std::string> group, std::optional<USER_ROLE> role, std::optional<bool> mustChangePassword) {
		const auto iterator = UsernameHashMap.find(username);
		if (iterator == UsernameHashMap.end())
			throw UserNotFoundException(username);

		User user = iterator->second;
		const bool invalidate = password.has_value() || (role.has_value() && *role != user.role());
		if (password.has_value())
			user.setNewHashedPassword(hashPassword(*password));
		if (group.has_value())
			user.setGroup(*group);
		if (role.has_value())
			user.setRole(*role);
		if (mustChangePassword.has_value())
			user.setMustChangePassword(*mustChangePassword);

		DBController->changeUser(user);
		iterator->second = user;
		if (invalidate)
			invalidateTokens(username);
		return user;
	}

	bool AuthenticationService::changeOwnPassword(const std::string& username, const std::string& oldPassword, const std::string& newPassword) {
		const auto iterator = UsernameHashMap.find(username);
		if (iterator == UsernameHashMap.end())
			throw UserNotFoundException(username);

		if (crypto_pwhash_str_verify(iterator->second.hashedPassword().c_str(), oldPassword.c_str(), oldPassword.size()) != 0)
			return false;

		User user = iterator->second;
		user.setNewHashedPassword(hashPassword(newPassword));
		user.setMustChangePassword(false);
		DBController->changeUser(user);
		iterator->second = user;
		return true;
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
