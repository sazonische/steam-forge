#include "app/session_access.h"

#include "app/account_library.h"
#include "steam/jwt_claims.h"

namespace SteamForge {

namespace {
	// Refresh slightly early, so the token does not expire between the requests of one operation.
	constexpr std::chrono::minutes ACCESS_TOKEN_SAFETY_MARGIN{1};
	// Steam answers a missing or rejected access token with this HTTP status instead of an EResult.
	constexpr int UNAUTHORIZED_STATUS = 401;

	bool NeedsNewAccessToken(const QString& accessToken) {
		const std::optional<JwtClaims> claims = ReadJwtClaims(accessToken);
		if (!claims.has_value() || !claims->expiresAt.has_value()) {
			return true;
		}
		return claims->expiresAt->addSecs(-std::chrono::duration_cast<std::chrono::seconds>(ACCESS_TOKEN_SAFETY_MARGIN).count()) <= QDateTime::currentDateTimeUtc();
	}
} // namespace

void EnsureFreshAccessToken(AuthenticationService& authenticationService, AccountLibrary& library, AccountSession& session, QObject& context, SessionAccessHandler handler) {
	if (!NeedsNewAccessToken(session.accessToken)) {
		handler({});
		return;
	}
	RenewAccessToken(authenticationService, library, session, context, std::move(handler));
}

void RenewAccessToken(AuthenticationService& authenticationService, AccountLibrary& library, AccountSession& session, QObject& context, SessionAccessHandler handler) {
	if (HasExpired(session.refreshToken, QDateTime::currentDateTimeUtc())) {
		handler(std::unexpected(SessionAccessFailure{.kind = SessionAccessFailureKind::SIGN_IN_REQUIRED, .error = {}}));
		return;
	}
	authenticationService.GenerateAccessTokenForApp(session.refreshToken, session.steamId, false, context, [&library, &session, resultHandler = std::move(handler)](const std::expected<AppAccessTokens, WebApiError>& tokens) {
		if (!tokens.has_value()) {
			const SessionAccessFailureKind kind = IsSignInRequired(tokens.error()) ? SessionAccessFailureKind::SIGN_IN_REQUIRED : SessionAccessFailureKind::RESPONSE_ERROR;
			resultHandler(std::unexpected(SessionAccessFailure{.kind = kind, .error = tokens.error()}));
			return;
		}
		session.accessToken = tokens->accessToken;
		if (!tokens->refreshToken.isEmpty()) {
			session.refreshToken = tokens->refreshToken;
		}
		// A renewed refresh token replaces the old one on the server, so it is saved before it is used.
		if (const std::expected<void, StorageError> saved = library.SaveSession(session); !saved.has_value()) {
			resultHandler(std::unexpected(SessionAccessFailure{.kind = SessionAccessFailureKind::STORAGE_ERROR, .error = {}}));
			return;
		}
		resultHandler({});
	});
}

bool IsSignInRequired(const WebApiError& error) {
	if (error.kind == WebApiErrorKind::HTTP_STATUS) {
		return error.httpStatus == UNAUTHORIZED_STATUS;
	}
	return error.kind == WebApiErrorKind::STEAM_RESULT && (error.steamResult == SteamResult::ACCESS_DENIED || error.steamResult == SteamResult::REVOKED || error.steamResult == SteamResult::EXPIRED || error.steamResult == SteamResult::NOT_LOGGED_ON);
}

} // namespace SteamForge
