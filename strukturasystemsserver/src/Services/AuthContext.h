#pragma once

#include <optional>
#include <utility>

#include "../Entities/db/ApiToken.hpp"
#include "../Entities/db/User.hpp"

namespace StructuraSystems::Server
{
	/// Name of the request attribute in which the JwtFilter stores the AuthContext (a std::shared_ptr<const AuthContext>).
	inline constexpr const char* AUTH_CONTEXT_ATTRIBUTE = "structura.authContext";

	/// Who sends a request: a user with a login session or an application with an API token. Exactly one of the two
	/// is set, so code that needs a user cannot get one for a token by accident.
	class AuthContext
	{
	public:
		enum class Kind { Session, ApiToken };

		static AuthContext forSession(User user)
		{
			AuthContext context(Kind::Session);
			context.SessionUser = std::move(user);
			return context;
		}

		static AuthContext forApiToken(ApiToken token)
		{
			AuthContext context(Kind::ApiToken);
			context.Token = std::move(token);
			return context;
		}

		Kind kind() const { return ContextKind; }
		bool isSession() const { return ContextKind == Kind::Session; }
		bool isApiToken() const { return ContextKind == Kind::ApiToken; }

		/// The user of a session, nullptr for an API token.
		const User* user() const { return SessionUser ? &*SessionUser : nullptr; }
		/// The API token, nullptr for a session.
		const ApiToken* apiToken() const { return Token ? &*Token : nullptr; }

	private:
		explicit AuthContext(Kind kind) : ContextKind(kind) {}

		Kind ContextKind;
		std::optional<User> SessionUser;
		std::optional<ApiToken> Token;
	};
}
