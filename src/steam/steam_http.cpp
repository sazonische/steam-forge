#include "steam/steam_http.h"

namespace SteamForge {

namespace {
	constexpr QByteArrayView ACCEPT_HEADER{"application/json, text/plain, */*"};
	constexpr QByteArrayView USER_AGENT{"okhttp/4.9.2"};
	// The Steam mobile application 3.10.9 identifies itself with these cookies.
	constexpr QByteArrayView MOBILE_CLIENT_COOKIE{"mobileClient=android; mobileClientVersion=777777 3.10.9"};
	constexpr QByteArrayView COOKIE_SEPARATOR{"; "};
	constexpr QLatin1StringView RUSSIAN_STEAM_LANGUAGE{"russian"};
	constexpr QLatin1StringView ENGLISH_STEAM_LANGUAGE{"english"};
	constexpr std::chrono::milliseconds REQUEST_TIMEOUT{std::chrono::seconds(30)};
	constexpr qint64 MAX_RESPONSE_SIZE = qint64{4} << 20;
	constexpr int FIRST_SUCCESS_STATUS = 200;
	constexpr int LAST_SUCCESS_STATUS = 299;
	constexpr const char* RESPONSE_TOO_LARGE_PROPERTY = "steamForgeResponseTooLarge";

	// QNetworkReply::errorString() quotes the whole URL with the access token, so only the error code is kept.
	QString NetworkErrorName(const QNetworkReply& reply) {
		const char* errorName = QMetaEnum::fromType<QNetworkReply::NetworkError>().valueToKey(reply.error());
		return errorName != nullptr ? QString::fromLatin1(errorName) : QString::number(reply.error());
	}
} // namespace

void ApplyMobileClientHeaders(QNetworkRequest& networkRequest, const QByteArray& extraCookies) {
	networkRequest.setTransferTimeout(REQUEST_TIMEOUT);
	networkRequest.setRawHeader("Accept", ACCEPT_HEADER.toByteArray());
	networkRequest.setRawHeader("User-Agent", USER_AGENT.toByteArray());
	QByteArray cookies = MOBILE_CLIENT_COOKIE.toByteArray();
	if (!extraCookies.isEmpty()) {
		cookies += COOKIE_SEPARATOR.toByteArray() + extraCookies;
	}
	networkRequest.setRawHeader("Cookie", cookies);
	// Cookies of one account must never reach a request of another, so the shared cookie jar is not used.
	networkRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	networkRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
}

QLatin1StringView InterfaceSteamLanguage() {
	return QLocale().language() == QLocale::Russian ? RUSSIAN_STEAM_LANGUAGE : ENGLISH_STEAM_LANGUAGE;
}

QByteArray EncodeParameters(const RequestParameters& parameters) {
	QByteArrayList encodedParameters;
	encodedParameters.reserve(parameters.size());
	for (const auto& [name, value] : parameters) {
		encodedParameters.append(QUrl::toPercentEncoding(name) + '=' + QUrl::toPercentEncoding(value));
	}
	return encodedParameters.join('&');
}

void WatchReply(QNetworkReply* reply, QObject& context, std::function<void(QNetworkReply&)> finishedHandler) {
	QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 receivedBytes, qint64) {
		if (receivedBytes > MAX_RESPONSE_SIZE) {
			reply->setProperty(RESPONSE_TOO_LARGE_PROPERTY, true);
			reply->abort();
		}
	});
	QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
	QObject::connect(&context, &QObject::destroyed, reply, &QNetworkReply::abort);
	QObject::connect(reply, &QNetworkReply::finished, &context, [reply, handler = std::move(finishedHandler)]() {
		handler(*reply);
	});
}

std::optional<WebApiError> ReplyFailure(QNetworkReply& reply) {
	if (reply.property(RESPONSE_TOO_LARGE_PROPERTY).toBool()) {
		return WebApiError{.kind = WebApiErrorKind::RESPONSE_TOO_LARGE, .httpStatus = 0, .steamResult = SteamResult::UNKNOWN, .message = {}};
	}
	if (reply.error() == QNetworkReply::OperationCanceledError) {
		return WebApiError{.kind = WebApiErrorKind::TIMEOUT, .httpStatus = 0, .steamResult = SteamResult::UNKNOWN, .message = NetworkErrorName(reply)};
	}
	const QVariant statusAttribute = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute);
	if (!statusAttribute.isValid()) {
		return WebApiError{.kind = WebApiErrorKind::NETWORK, .httpStatus = 0, .steamResult = SteamResult::UNKNOWN, .message = NetworkErrorName(reply)};
	}
	const int httpStatus = statusAttribute.toInt();
	if (httpStatus < FIRST_SUCCESS_STATUS || httpStatus > LAST_SUCCESS_STATUS) {
		return WebApiError{.kind = WebApiErrorKind::HTTP_STATUS, .httpStatus = httpStatus, .steamResult = SteamResult::UNKNOWN, .message = {}};
	}
	return std::nullopt;
}

} // namespace SteamForge
