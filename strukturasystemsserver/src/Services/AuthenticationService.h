
#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>
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
		/// mustChangePassword. The tokens stay valid. Returns false if the old password is wrong.
		bool changeOwnPassword(const std::string& username, const std::string& oldPassword, const std::string& newPassword);
	private:
		AuthenticationService();

		std::string decode64(const std::string& value);
		std::string encode64(const std::string& value);

		static std::shared_ptr<AuthenticationService> Instance;

		void readUserFromDatabase();
		void createInitialAdministrator();
		void invalidateTokens(const std::string& username);
		static std::string hashPassword(const std::string& password);

		std::map<std::string, User> UsernameHashMap;
		std::map<std::string, std::shared_ptr<InstanceStorage>> UsernameInstanceMap;
		std::map<std::string, std::shared_ptr<InstanceStorage>> ValidInstances;

		DataBaseController* DBController;
	};

}