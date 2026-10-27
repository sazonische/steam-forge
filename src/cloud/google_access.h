#pragma once

#include "cloud/google_oauth.h"

namespace SteamForge {

// Keeps a fresh access token for one Google sign-in. The refresh token stays the same until it is revoked; requests
// that need a token while one is being refreshed wait for the same refresh.
class GoogleAccess final : public QObject {
	Q_OBJECT

public:
	GoogleAccess(GoogleTransport& transport, GoogleEndpoints endpoints, GoogleClient client, QString savedRefreshToken, QObject* parent = nullptr);

	// The tokens of a sign-in that just finished, so the first request needs no refresh.
	void UseTokens(const GoogleTokens& tokens);
	// The handler gets a token that stays valid for at least a minute.
	void RequestAccessToken(QObject& context, GoogleHandler<QString> handler);
	// Google refused the current token: the next request gets a new one.
	void ForgetAccessToken();
	[[nodiscard]] const QString& RefreshToken() const;

Q_SIGNALS:
	// The refresh token was revoked or expired.
	void SignInRequired();

private:
	struct WaitingRequest {
		QPointer<QObject> context;
		GoogleHandler<QString> handler;
	};

	void FinishRefresh(const std::expected<QString, GoogleError>& outcome);

	GoogleTransport& googleTransport;
	GoogleEndpoints googleEndpoints;
	GoogleClient googleClient;
	QString refreshToken;
	QString accessToken;
	QDateTime accessTokenExpiresAt;
	std::vector<WaitingRequest> waitingRequests;
	bool isRefreshing = false;
};

} // namespace SteamForge
