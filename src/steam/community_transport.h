#pragma once

#include "steam/steam_http.h"

namespace SteamForge {

// The Steam Community sign-in of one account: the MobileApp access token is its steamLoginSecure cookie.
struct CommunitySession {
	quint64 steamId = 0;
	QString accessToken;
	QString sessionId;
};

struct CommunityRequest {
	// Relative to the Steam Community origin, for example "mobileconf/getlist".
	QString path;
	WebApiHttpMethod httpMethod = WebApiHttpMethod::GET;
	// The query of GET or the form body of POST.
	RequestParameters parameters;
	CommunitySession session;
	// Cookies Steam Community set earlier for this session, such as the market eligibility.
	QList<QNetworkCookie> extraCookies;
};

using CommunityCookiesResponse = std::expected<QList<QNetworkCookie>, WebApiError>;
using CommunityCookiesHandler = std::function<void(CommunityCookiesResponse)>;

// Network boundary of steamcommunity.com requests; tests replace it with prepared responses.
class CommunityTransport {
public:
	CommunityTransport() = default;
	virtual ~CommunityTransport() = default;
	Q_DISABLE_COPY_MOVE(CommunityTransport)

	// The handler runs on the event loop and is dropped when the context object is destroyed first.
	virtual void Send(const CommunityRequest& request, QObject& context, WebApiResponseHandler handler) = 0;
	// For a GET that answers with a redirect and sets cookies, such as the market eligibility check.
	virtual void FetchCookies(const CommunityRequest& request, QObject& context, CommunityCookiesHandler handler) = 0;
};

// Requests over HTTPS to steamcommunity.com with the cookies of the request's own session only.
class SteamCommunityTransport final : public QObject, public CommunityTransport {
	Q_OBJECT

public:
	explicit SteamCommunityTransport(QObject* parent = nullptr);
	// Tests point the transport at a local server instead of steamcommunity.com.
	explicit SteamCommunityTransport(QString communityOrigin, QObject* parent = nullptr);

	void Send(const CommunityRequest& request, QObject& context, WebApiResponseHandler handler) override;
	void FetchCookies(const CommunityRequest& request, QObject& context, CommunityCookiesHandler handler) override;

private:
	[[nodiscard]] QUrl RequestUrl(const CommunityRequest& request) const;

	QString origin;
	QNetworkAccessManager networkAccessManager;
};

// A random value for the sessionid cookie, as node-steam-session creates it.
[[nodiscard]] QString CreateCommunitySessionId();

} // namespace SteamForge
