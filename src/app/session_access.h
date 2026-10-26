#pragma once

#include "domain/account_session.h"
#include "steam/authentication_service.h"

namespace SteamForge {

class AccountLibrary;

enum class SessionAccessFailureKind {
	SIGN_IN_REQUIRED,
	STORAGE_ERROR,
	RESPONSE_ERROR
};

struct SessionAccessFailure {
	SessionAccessFailureKind kind = SessionAccessFailureKind::RESPONSE_ERROR;
	WebApiError error;
};

using SessionAccessHandler = std::function<void(std::expected<void, SessionAccessFailure>)>;

// Renews the access token of the session when it is about to expire and saves the session before
// the renewed tokens are used. The handler is dropped when the context object is destroyed first.
void EnsureFreshAccessToken(AuthenticationService& authenticationService, AccountLibrary& library, AccountSession& session, QObject& context, SessionAccessHandler handler);

// Renews the access token even when it has not expired yet, for a service that no longer accepts it.
void RenewAccessToken(AuthenticationService& authenticationService, AccountLibrary& library, AccountSession& session, QObject& context, SessionAccessHandler handler);

// Steam no longer accepts the tokens of the session; only a new sign-in helps.
[[nodiscard]] bool IsSignInRequired(const WebApiError& error);

} // namespace SteamForge
