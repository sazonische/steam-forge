#include "steam/community_transport.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView STEAM_COMMUNITY_ORIGIN{"https://steamcommunity.com"};
	constexpr QByteArrayView FORM_CONTENT_TYPE{"application/x-www-form-urlencoded; charset=UTF-8"};
	constexpr QLatin1StringView SESSION_COOKIES_PATTERN{"steamLoginSecure=%1; sessionid=%2; Steam_Language=%3"};
	constexpr QLatin1StringView ANONYMOUS_COOKIES_PATTERN{"Steam_Language=%1"};
	constexpr QLatin1StringView STEAM_LOGIN_VALUE_SEPARATOR{"||"};
	// 12 random bytes in hexadecimal, like node-steam-session.
	constexpr std::size_t SESSION_ID_WORD_COUNT = 3;
	constexpr int HEX_DIGITS_PER_WORD = 8;
	constexpr int HEX_BASE = 16;
	constexpr int FIRST_REDIRECT_STATUS = 300;
	constexpr int LAST_REDIRECT_STATUS = 399;
	constexpr QByteArrayView COOKIE_SEPARATOR{"; "};

	QByteArray SessionCookies(const CommunityRequest& request) {
		const CommunitySession& session = request.session;
		QByteArray cookies;
		// A request without a session, such as a public profile, carries no sign-in at all.
		if (session.accessToken.isEmpty()) {
			cookies = QString(ANONYMOUS_COOKIES_PATTERN).arg(InterfaceSteamLanguage()).toLatin1();
		} else {
			const QString steamLoginValue = QString::number(session.steamId) + STEAM_LOGIN_VALUE_SEPARATOR + session.accessToken;
			cookies = QString(SESSION_COOKIES_PATTERN).arg(QString::fromLatin1(QUrl::toPercentEncoding(steamLoginValue)), session.sessionId, InterfaceSteamLanguage()).toLatin1();
		}
		for (const QNetworkCookie& cookie : request.extraCookies) {
			cookies += COOKIE_SEPARATOR.toByteArray() + cookie.toRawForm(QNetworkCookie::NameAndValueOnly);
		}
		return cookies;
	}

	WebApiResponse ReadResponse(QNetworkReply& reply) {
		if (std::optional<WebApiError> failure = ReplyFailure(reply); failure.has_value()) {
			return std::unexpected(std::move(*failure));
		}
		return reply.readAll();
	}

	CommunityCookiesResponse ReadCookies(QNetworkReply& reply) {
		const int httpStatus = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const bool isRedirect = httpStatus >= FIRST_REDIRECT_STATUS && httpStatus <= LAST_REDIRECT_STATUS;
		if (std::optional<WebApiError> failure = ReplyFailure(reply); failure.has_value() && !isRedirect) {
			return std::unexpected(std::move(*failure));
		}
		return reply.header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>();
	}
} // namespace

SteamCommunityTransport::SteamCommunityTransport(QObject* parent) :
	SteamCommunityTransport(STEAM_COMMUNITY_ORIGIN, parent) {
}

SteamCommunityTransport::SteamCommunityTransport(QString communityOrigin, QObject* parent) :
	QObject(parent),
	origin(std::move(communityOrigin)) {
}

void SteamCommunityTransport::Send(const CommunityRequest& request, QObject& context, WebApiResponseHandler handler) {
	const QByteArray encodedParameters = EncodeParameters(request.parameters);
	QNetworkRequest networkRequest(RequestUrl(request));
	ApplyMobileClientHeaders(networkRequest, SessionCookies(request));

	QNetworkReply* reply = nullptr;
	if (request.httpMethod == WebApiHttpMethod::GET) {
		reply = networkAccessManager.get(networkRequest);
	} else {
		networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, FORM_CONTENT_TYPE.toByteArray());
		reply = networkAccessManager.post(networkRequest, encodedParameters);
	}
	WatchReply(reply, context, [responseHandler = std::move(handler)](QNetworkReply& finishedReply) {
		responseHandler(ReadResponse(finishedReply));
	});
}

void SteamCommunityTransport::FetchCookies(const CommunityRequest& request, QObject& context, CommunityCookiesHandler handler) {
	QNetworkRequest networkRequest(RequestUrl(request));
	ApplyMobileClientHeaders(networkRequest, SessionCookies(request));
	QNetworkReply* reply = networkAccessManager.get(networkRequest);
	WatchReply(reply, context, [cookiesHandler = std::move(handler)](QNetworkReply& finishedReply) {
		cookiesHandler(ReadCookies(finishedReply));
	});
}

QUrl SteamCommunityTransport::RequestUrl(const CommunityRequest& request) const {
	QUrl url(origin + QLatin1Char('/') + request.path);
	const QByteArray encodedParameters = EncodeParameters(request.parameters);
	if (request.httpMethod == WebApiHttpMethod::GET && !encodedParameters.isEmpty()) {
		url.setQuery(QString::fromLatin1(encodedParameters), QUrl::StrictMode);
	}
	return url;
}

QString CreateCommunitySessionId() {
	std::array<quint32, SESSION_ID_WORD_COUNT> randomWords{};
	QRandomGenerator::system()->generate(randomWords.begin(), randomWords.end());
	QString sessionId;
	for (const quint32 randomWord : randomWords) {
		sessionId += QStringLiteral("%1").arg(randomWord, HEX_DIGITS_PER_WORD, HEX_BASE, QLatin1Char('0'));
	}
	return sessionId;
}

} // namespace SteamForge
