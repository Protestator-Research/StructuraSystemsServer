
#pragma once

#include <memory>
#include <optional>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>

#include "../Entities/db/User.hpp"
#include "SessionStore.h"

namespace StructuraSystems::Server
{
	class DataBaseController;

	class UserAlreadyExistsException : public std::runtime_error {
	public:
		explicit UserAlreadyExistsException(const std::string& username) : std::runtime_error("User already exists: " + username) {}
	};

	class UserNotFoundException : public std::runtime_error {
	public:
		explicit UserNotFoundException(const std::string& username) : std::runtime_error("User not found: " + username) {}
	};

	class AuthenticationService
	{
	public:
		virtual ~AuthenticationService() = default;

		static std::shared_ptr<AuthenticationService> getInstance();

		/// Checks the credentials and returns the token of a new session. Throws std::runtime_error
		/// with the same message for an unknown user and a wrong password; both take about the same time.
		std::string authenticateUserWith(const std::string& name, const std::string& password);

		/// Throws UserAlreadyExistsException if the name is taken. The database is written first;
		/// on a database failure the in-memory state stays unchanged.
		void addUser(std::string username, std::string password, USER_ROLE role = USER_ROLE::USER, std::string group = "", bool mustChangePassword = false);

		bool isTokenAuthenticated(std::string bearerToken);
		bool isUserAuthenticationAdministrator(std::string bearerToken);

		/// Returns the user belonging to the token or std::nullopt for unknown tokens and deleted users.
		std::optional<User> getUserFromDatabase(std::string bearerToken);
		std::optional<User> findUser(const std::string& username);
		std::vector<User> getUsersFromDatabase();
		size_t countAdministrators();

		/// Throws UserNotFoundException. Also invalidates all tokens of the user.
		User deleteUserFromDatabase(std::string username);
		/// Applies only the given fields. A password or role change invalidates the user's tokens.
		/// Throws UserNotFoundException.
		User changeUserInDatabase(std::string username, std::optional<std::string> password, std::optional<std::string> group, std::optional<USER_ROLE> role, std::optional<bool> mustChangePassword = std::nullopt);
		/// Changes the password of the calling user after verifying the old one and clears
		/// mustChangePassword. The session with currentToken stays valid, all other sessions of the
		/// user are invalidated. Returns false if the old password is wrong.
		bool changeOwnPassword(const std::string& username, const std::string& oldPassword, const std::string& newPassword, const std::string& currentToken);

		/// Removes expired sessions.
		void purgeExpiredSessions();
	private:
		AuthenticationService();

		void readUserFromDatabase();
		void createInitialAdministrator();
		static std::string hashPassword(const std::string& password);

		/// Guards UsernameHashMap. The SessionStore has its own mutex; the lock order is Mutex before
		/// the SessionStore's mutex. Password hashing is done outside of the lock. Not recursive.
		mutable std::shared_mutex Mutex;
		std::map<std::string, User> UsernameHashMap;
		SessionStore Sessions;
		/// Hash of a random password, verified for unknown users so that they take as long as known ones.
		std::string DummyHash;

		DataBaseController* DBController;
	};

}