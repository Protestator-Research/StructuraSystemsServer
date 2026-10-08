#include "AuthenticationService.h"

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sodium.h>

#include "../Controller/DataBaseController.h"
#include "ServerConfig.h"

namespace StructuraSystems::Server {
	AuthenticationService::AuthenticationService()
		: Sessions(ServerConfig::get().SessionIdleTimeout, ServerConfig::get().SessionMaxLifetime) {
		DBController = DataBaseController::getInstance();
		readUserFromDatabase();

		// Same cost parameters as the real hashes (hashPassword), so the unknown-user path takes as long.
		unsigned char randomPassword[24];
		randombytes_buf(randomPassword, sizeof(randomPassword));
		DummyHash = hashPassword(std::string(reinterpret_cast<const char*>(randomPassword), sizeof(randomPassword)));

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
		struct conreteAuthService : public AuthenticationService {};
		// A function-local static is initialised exactly once, even with concurrent first calls.
		static const std::shared_ptr<AuthenticationService> instance = std::make_shared<conreteAuthService>();
		return instance;
	}

	std::string AuthenticationService::authenticateUserWith(const std::string& name, const std::string& password) {
		std::string securityString;
		{
			std::shared_lock lock(Mutex);
			const auto userIterator = UsernameHashMap.find(name);
			if (userIterator != UsernameHashMap.end())
				securityString = userIterator->second.hashedPassword();
		}

		// The expensive hash runs outside of the lock. Unknown users are verified against a dummy hash.
		const bool known = !securityString.empty();
		const auto check_value = crypto_pwhash_str_verify(
			(known ? securityString : DummyHash).c_str(),
			password.c_str(),
			password.size()
		);
		if (!known || check_value != 0)
			throw std::runtime_error("Username or password wrong.");

		// The user may have been deleted or changed while the hash was computed. Creating the session
		// under the lock guarantees that a later revoke sees it.
		std::shared_lock lock(Mutex);
		const auto userIterator = UsernameHashMap.find(name);
		if (userIterator == UsernameHashMap.end() || userIterator->second.hashedPassword() != securityString)
			throw std::runtime_error("Username or password wrong.");
		return Sessions.createSession(name);
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

	void AuthenticationService::addUser(std::string username, std::string password, USER_ROLE role, std::string group, bool mustChangePassword)
	{
		{
			std::shared_lock lock(Mutex);
			if (UsernameHashMap.contains(username))
				throw UserAlreadyExistsException(username);
		}

		User user(username, hashPassword(password), std::move(group), role);
		user.setMustChangePassword(mustChangePassword);

		std::unique_lock lock(Mutex);
		if (UsernameHashMap.contains(username))
			throw UserAlreadyExistsException(username);
		DBController->addUser(user);
		UsernameHashMap[username] = user;
	}

	bool AuthenticationService::isTokenAuthenticated(std::string bearerToken) {
		return getUserFromDatabase(bearerToken).has_value();
	}

	bool AuthenticationService::isUserAuthenticationAdministrator(std::string bearerToken) {
		const auto user = getUserFromDatabase(bearerToken);
		return user.has_value() && user->role() == USER_ROLE::GLOBAL_ADMINISTRATOR;
	}

	std::optional<User> AuthenticationService::getUserFromDatabase(std::string bearerToken) {
		const auto username = Sessions.validate(bearerToken);
		if (!username)
			return std::nullopt;
		// A token of a user that no longer exists is invalid.
		return findUser(*username);
	}

	std::optional<User> AuthenticationService::findUser(const std::string& username) {
		std::shared_lock lock(Mutex);
		const auto user = UsernameHashMap.find(username);
		if (user == UsernameHashMap.end())
			return std::nullopt;
		return user->second;
	}

	std::vector<User> AuthenticationService::getUsersFromDatabase() {
		std::shared_lock lock(Mutex);
		std::vector<User> users;

		for (const auto& user_pair : UsernameHashMap)
			users.push_back(user_pair.second);

		return users;
	}

	size_t AuthenticationService::countAdministrators() {
		std::shared_lock lock(Mutex);
		size_t count = 0;
		for (const auto& user_pair : UsernameHashMap)
			if (user_pair.second.role() == USER_ROLE::GLOBAL_ADMINISTRATOR)
				++count;
		return count;
	}

	User AuthenticationService::deleteUserFromDatabase(std::string username) {
		std::unique_lock lock(Mutex);
		const auto iterator = UsernameHashMap.find(username);
		if (iterator == UsernameHashMap.end())
			throw UserNotFoundException(username);

		User user = iterator->second;
		DBController->removeUser(username);
		UsernameHashMap.erase(username);
		Sessions.revokeAllFor(username);

		return user;
	}

	User AuthenticationService::changeUserInDatabase(std::string username, std::optional<std::string> password, std::optional<std::string> group, std::optional<USER_ROLE> role, std::optional<bool> mustChangePassword) {
		// The expensive hash is computed before the lock is taken.
		std::optional<std::string> newHash;
		if (password.has_value())
			newHash = hashPassword(*password);

		std::unique_lock lock(Mutex);
		const auto iterator = UsernameHashMap.find(username);
		if (iterator == UsernameHashMap.end())
			throw UserNotFoundException(username);

		User user = iterator->second;
		const bool invalidate = password.has_value() || (role.has_value() && *role != user.role());
		if (newHash.has_value())
			user.setNewHashedPassword(*newHash);
		if (group.has_value())
			user.setGroup(*group);
		if (role.has_value())
			user.setRole(*role);
		if (mustChangePassword.has_value())
			user.setMustChangePassword(*mustChangePassword);

		DBController->changeUser(user);
		iterator->second = user;
		if (invalidate)
			Sessions.revokeAllFor(username);
		return user;
	}

	bool AuthenticationService::changeOwnPassword(const std::string& username, const std::string& oldPassword, const std::string& newPassword, const std::string& currentToken) {
		std::string oldHash;
		{
			std::shared_lock lock(Mutex);
			const auto iterator = UsernameHashMap.find(username);
			if (iterator == UsernameHashMap.end())
				throw UserNotFoundException(username);
			oldHash = iterator->second.hashedPassword();
		}

		if (crypto_pwhash_str_verify(oldHash.c_str(), oldPassword.c_str(), oldPassword.size()) != 0)
			return false;
		const auto newHash = hashPassword(newPassword);

		std::unique_lock lock(Mutex);
		const auto iterator = UsernameHashMap.find(username);
		if (iterator == UsernameHashMap.end())
			throw UserNotFoundException(username);
		// The password was changed concurrently, so the verified old password is no longer the current one.
		if (iterator->second.hashedPassword() != oldHash)
			return false;

		User user = iterator->second;
		user.setNewHashedPassword(newHash);
		user.setMustChangePassword(false);
		DBController->changeUser(user);
		iterator->second = user;
		Sessions.revokeAllForExcept(username, currentToken);
		return true;
	}

	void AuthenticationService::purgeExpiredSessions() {
		Sessions.purgeExpired();
	}
}