#include "cloud/google_access.h"

namespace SteamForge {

namespace {
	// A token that expires sooner may run out while a request is on its way.
	constexpr qint64 EXPIRY_MARGIN_SECONDS = 60;
} // namespace

GoogleAccess::GoogleAccess(GoogleTransport& transport, GoogleEndpoints endpoints, GoogleClient client, QString savedRefreshToken, QObject* parent) :
	QObject(parent),
	googleTransport(transport),
	googleEndpoints(std::move(endpoints)),
	googleClient(std::move(client)),
	refreshToken(std::move(savedRefreshToken)) {
}

void GoogleAccess::UseTokens(const GoogleTokens& tokens) {
	accessToken = tokens.accessToken;
	accessTokenExpiresAt = tokens.accessTokenExpiresAt;
}

void GoogleAccess::RequestAccessToken(QObject& context, GoogleHandler<QString> handler) {
	if (!accessToken.isEmpty() && QDateTime::currentDateTimeUtc().addSecs(EXPIRY_MARGIN_SECONDS) < accessTokenExpiresAt) {
		handler(accessToken);
		return;
	}
	waitingRequests.push_back(WaitingRequest{.context = &context, .handler = std::move(handler)});
	if (isRefreshing) {
		return;
	}
	isRefreshing = true;
	googleTransport.Send(TokenRefreshRequest(googleEndpoints, googleClient, refreshToken), *this, [this](GoogleResponse response) {
		if (!response.has_value()) {
			FinishRefresh(std::unexpected(response.error()));
			return;
		}
		const std::optional<GoogleTokens> tokens = ParseTokenResponse(*response, QDateTime::currentDateTimeUtc());
		if (!tokens.has_value()) {
			FinishRefresh(std::unexpected(GoogleError{.kind = GoogleErrorKind::MALFORMED_RESPONSE, .httpStatus = 0, .reason = {}}));
			return;
		}
		UseTokens(*tokens);
		FinishRefresh(tokens->accessToken);
	});
}

void GoogleAccess::ForgetAccessToken() {
	accessToken.clear();
	accessTokenExpiresAt = {};
}

const QString& GoogleAccess::RefreshToken() const {
	return refreshToken;
}

void GoogleAccess::FinishRefresh(const std::expected<QString, GoogleError>& outcome) {
	isRefreshing = false;
	const std::vector<WaitingRequest> finishedRequests = std::exchange(waitingRequests, {});
	for (const WaitingRequest& request : finishedRequests) {
		if (request.context != nullptr) {
			request.handler(outcome);
		}
	}
	if (!outcome.has_value() && outcome.error().kind == GoogleErrorKind::SIGN_IN_REQUIRED) {
		Q_EMIT SignInRequired();
	}
}

} // namespace SteamForge
