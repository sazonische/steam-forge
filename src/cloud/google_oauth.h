#pragma once

#include "cloud/google_endpoints.h"
#include "cloud/google_transport.h"

namespace SteamForge {

// The OAuth client SteamForge signs in to Google with. A desktop program cannot keep a secret, so the client
// secret is only what Google asks for, not a protection.
struct GoogleClient {
	QString clientId;
	QString clientSecret;
};

// The hidden folder of SteamForge on Google Drive and the address of the account, nothing else of the user's Drive.
inline constexpr QLatin1StringView DRIVE_APPLICATION_DATA_SCOPE{"https://www.googleapis.com/auth/drive.appdata"};

// RFC 7636: the verifier stays in SteamForge, Google only sees its SHA-256 until the code is exchanged.
struct ProofKey {
	QString verifier;
	QString challenge;
};

[[nodiscard]] std::optional<ProofKey> GenerateProofKey();
[[nodiscard]] ProofKey ProofKeyFromVerifier(const QString& verifier);
// Ties the answer of the sign-in page to the request this SteamForge made.
[[nodiscard]] std::optional<QString> GenerateState();

[[nodiscard]] QUrl AuthorizationUrl(const GoogleEndpoints& endpoints, const GoogleClient& client, const QString& redirectUri, const ProofKey& proofKey, const QString& state);

struct GoogleTokens {
	QString refreshToken;
	QString accessToken;
	QDateTime accessTokenExpiresAt;
	QStringList grantedScopes;
	// From the ID token; tells the user which Google account is connected.
	QString email;
};

[[nodiscard]] GoogleRequest CodeExchangeRequest(const GoogleEndpoints& endpoints, const GoogleClient& client, const QString& authorizationCode, const ProofKey& proofKey, const QString& redirectUri);
[[nodiscard]] GoogleRequest TokenRefreshRequest(const GoogleEndpoints& endpoints, const GoogleClient& client, const QString& refreshToken);
[[nodiscard]] GoogleRequest RevocationRequest(const GoogleEndpoints& endpoints, const QString& token);
// An answer to a refresh carries no refresh token and no ID token; the caller keeps the ones it has.
[[nodiscard]] std::optional<GoogleTokens> ParseTokenResponse(const QByteArray& responseJson, const QDateTime& receivedAt);
// Google lets the user untick the Drive permission on its consent page.
[[nodiscard]] bool GrantsDriveAccess(const GoogleTokens& tokens);

} // namespace SteamForge
