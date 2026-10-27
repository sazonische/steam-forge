#include "cloud/google_transport.h"

#include "common/constants.h"
#include "common/version.h"
#include "steam/steam_http.h"

namespace SteamForge {

namespace {
	constexpr std::chrono::milliseconds REQUEST_TIMEOUT{std::chrono::seconds(30)};
	constexpr QLatin1StringView USER_AGENT_PATTERN{"%1/%2"};
	constexpr QByteArrayView AUTHORIZATION_HEADER{"Authorization"};
	constexpr QLatin1StringView BEARER_PATTERN{"Bearer %1"};
	constexpr int MAXIMUM_REDIRECTS = 2;
	constexpr int UNAUTHORIZED_STATUS = 401;
	constexpr QLatin1StringView ERROR_FIELD{"error"};
	constexpr QLatin1StringView STATUS_FIELD{"status"};
	constexpr QLatin1StringView INVALID_GRANT_REASON{"invalid_grant"};

	// The sign-in endpoints answer {"error": "invalid_grant"}, Drive answers {"error": {"status": "NOT_FOUND"}}.
	QString ErrorReason(const QByteArray& body) {
		const QJsonValue error = QJsonDocument::fromJson(body).object().value(ERROR_FIELD);
		if (error.isString()) {
			return error.toString();
		}
		return error.toObject().value(STATUS_FIELD).toString();
	}

	GoogleError TranslateFailure(const WebApiError& failure, const QByteArray& body) {
		switch (failure.kind) {
			case WebApiErrorKind::TIMEOUT:
				return GoogleError{.kind = GoogleErrorKind::TIMEOUT, .httpStatus = 0, .reason = {}};
			case WebApiErrorKind::RESPONSE_TOO_LARGE:
				return GoogleError{.kind = GoogleErrorKind::RESPONSE_TOO_LARGE, .httpStatus = 0, .reason = {}};
			case WebApiErrorKind::HTTP_STATUS: {
				const QString reason = ErrorReason(body);
				if (reason == INVALID_GRANT_REASON) {
					return GoogleError{.kind = GoogleErrorKind::SIGN_IN_REQUIRED, .httpStatus = failure.httpStatus, .reason = reason};
				}
				const GoogleErrorKind kind = failure.httpStatus == UNAUTHORIZED_STATUS ? GoogleErrorKind::UNAUTHORIZED : GoogleErrorKind::HTTP_STATUS;
				return GoogleError{.kind = kind, .httpStatus = failure.httpStatus, .reason = reason};
			}
			default:
				return GoogleError{.kind = GoogleErrorKind::NETWORK, .httpStatus = 0, .reason = failure.message};
		}
	}
} // namespace

NetworkGoogleTransport::NetworkGoogleTransport(QObject* parent) :
	QObject(parent) {
}

void NetworkGoogleTransport::Send(const GoogleRequest& request, QObject& context, GoogleResponseHandler handler) {
	QNetworkRequest networkRequest(request.url);
	networkRequest.setTransferTimeout(REQUEST_TIMEOUT);
	networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QString(USER_AGENT_PATTERN).arg(APPLICATION_NAME, APPLICATION_VERSION));
	networkRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	networkRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
	networkRequest.setMaximumRedirectsAllowed(MAXIMUM_REDIRECTS);
	if (!request.accessToken.isEmpty()) {
		networkRequest.setRawHeader(AUTHORIZATION_HEADER.toByteArray(), QString(BEARER_PATTERN).arg(request.accessToken).toLatin1());
	}
	if (!request.contentType.isEmpty()) {
		networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, request.contentType);
	}
	QNetworkReply* reply = nullptr;
	switch (request.method) {
		case GoogleHttpMethod::GET:
			reply = networkAccessManager.get(networkRequest);
			break;
		case GoogleHttpMethod::POST:
			reply = networkAccessManager.post(networkRequest, request.body);
			break;
		case GoogleHttpMethod::DELETE_RESOURCE:
			reply = networkAccessManager.deleteResource(networkRequest);
			break;
	}
	WatchReply(reply, context, [finishedHandler = std::move(handler)](QNetworkReply& finishedReply) {
		const std::optional<WebApiError> failure = ReplyFailure(finishedReply);
		QByteArray body = finishedReply.readAll();
		if (failure.has_value()) {
			finishedHandler(std::unexpected(TranslateFailure(*failure, body)));
			return;
		}
		finishedHandler(std::move(body));
	});
}

} // namespace SteamForge
