#pragma once

#include "steam/web_api_transport.h"

namespace SteamForge {

// EAuthSessionGuardType values (steammessages_auth.steamclient.proto).
enum class AuthGuardType : int {
	UNKNOWN = 0,
	NONE = 1,
	EMAIL_CODE = 2,
	DEVICE_CODE = 3,
	DEVICE_CONFIRMATION = 4,
	EMAIL_CONFIRMATION = 5,
	MACHINE_TOKEN = 6,
	LEGACY_MACHINE_AUTH = 7
};

struct PasswordRsaKey {
	QByteArray modulus;
	QByteArray publicExponent;
	quint64 timestamp = 0;
};

struct AllowedConfirmation {
	AuthGuardType type = AuthGuardType::UNKNOWN;
	// For an email code Steam sends the domain of the address here.
	QString message;
};

struct CredentialsAuthSession {
	quint64 clientId = 0;
	QByteArray requestId;
	std::chrono::milliseconds pollInterval{0};
	std::vector<AllowedConfirmation> allowedConfirmations;
	quint64 steamId = 0;
	// Accepted only by the calls that move the authenticator during this sign-in.
	QString weakToken;
};

struct AuthSessionPollResult {
	std::optional<quint64> newClientId;
	QString refreshToken;
	QString accessToken;
	QString accountName;
};

struct AppAccessTokens {
	QString accessToken;
	// Present only when Steam renewed the refresh token as well.
	QString refreshToken;
};

struct AuthSessionInfo {
	QString ipAddress;
	QString city;
	QString state;
	QString country;
	QString deviceFriendlyName;
	int platformType = 0;
	// Version of the sign-in request, part of the signed approval.
	int version = 0;
	bool isRequestorLocationMismatch = false;
	bool isHighUsageLogin = false;
	// The device that asks wants to stay signed in ("Remember me").
	bool isPersistenceRequested = false;
};

struct MobileConfirmation {
	int version = 0;
	quint64 clientId = 0;
	quint64 steamId = 0;
	QByteArray signature;
	bool isApproved = false;
	bool isPersistent = false;
};

template<typename Result>
using AuthenticationHandler = WebApiHandler<Result>;

// Typed IAuthenticationService calls with the platform data of the Steam mobile application.
class AuthenticationService final {
public:
	explicit AuthenticationService(WebApiTransport& webApiTransport);

	void GetPasswordRsaPublicKey(const QString& accountName, QObject& context, AuthenticationHandler<PasswordRsaKey> handler);
	void BeginAuthSessionViaCredentials(const QString& accountName, const QByteArray& encryptedPassword, quint64 encryptionTimestamp, QObject& context, AuthenticationHandler<CredentialsAuthSession> handler);
	void UpdateAuthSessionWithSteamGuardCode(quint64 clientId, quint64 steamId, const QString& code, AuthGuardType codeType, QObject& context, AuthenticationHandler<void> handler);
	void PollAuthSessionStatus(quint64 clientId, const QByteArray& requestId, QObject& context, AuthenticationHandler<AuthSessionPollResult> handler);
	void GenerateAccessTokenForApp(const QString& refreshToken, quint64 steamId, bool isRefreshTokenRenewalAllowed, QObject& context, AuthenticationHandler<AppAccessTokens> handler);
	void GetAuthSessionsForAccount(const QString& accessToken, QObject& context, AuthenticationHandler<std::vector<quint64>> handler);
	void GetAuthSessionInfo(const QString& accessToken, quint64 clientId, QObject& context, AuthenticationHandler<AuthSessionInfo> handler);
	void UpdateAuthSessionWithMobileConfirmation(const QString& accessToken, const MobileConfirmation& confirmation, QObject& context, AuthenticationHandler<void> handler);

private:
	WebApiTransport& transport;
};

} // namespace SteamForge
