#include "steam/community_transport.h"
#include "support/local_http_server.h"

namespace {
constexpr int RESPONSE_TIMEOUT_MILLISECONDS = 10000;
constexpr quint64 FIRST_STEAM_ID = 76561198000000001;
constexpr quint64 SECOND_STEAM_ID = 76561198000000002;
constexpr QByteArrayView JSON_BODY{R"({"success":true})"};
constexpr QByteArrayView OK_RESPONSE{"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 16\r\nConnection: close\r\n\r\n{\"success\":true}"};
// A cookie the server sets must not travel to the next request, which may belong to another account.
constexpr QByteArrayView COOKIE_SETTING_RESPONSE{"HTTP/1.1 200 OK\r\nSet-Cookie: steamLoginSecure=foreign; Path=/\r\nSet-Cookie: sessionid=foreign; Path=/\r\nContent-Length: 16\r\nConnection: close\r\n\r\n{\"success\":true}"};
constexpr QByteArrayView REDIRECT_RESPONSE{"HTTP/1.1 302 Found\r\nLocation: https://login.example/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
constexpr int FOUND_STATUS = 302;
constexpr QByteArrayView ELIGIBILITY_RESPONSE{"HTTP/1.1 302 Found\r\nLocation: https://steamcommunity.com/market/\r\nSet-Cookie: webTradeEligibility=%7B%22allowed%22%3A1%7D; Path=/\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};

SteamForge::CommunitySession Session(quint64 steamId, const QString& accessToken) {
	return {
		.steamId = steamId,
		.accessToken = accessToken,
		.sessionId = QStringLiteral("0123456789abcdef01234567"),
	};
}

SteamForge::WebApiResponse Send(SteamForge::SteamCommunityTransport& transport, const SteamForge::CommunityRequest& request) {
	QObject context;
	std::optional<SteamForge::WebApiResponse> response;
	transport.Send(request, context, [&response](SteamForge::WebApiResponse receivedResponse) {
		response = std::move(receivedResponse);
	});
	QDeadlineTimer deadline(RESPONSE_TIMEOUT_MILLISECONDS);
	while (!response.has_value() && !deadline.hasExpired()) {
		QTest::qWait(1);
	}
	return response.value_or(std::unexpected(SteamForge::WebApiError{.kind = SteamForge::WebApiErrorKind::TIMEOUT, .httpStatus = 0, .steamResult = SteamForge::SteamResult::UNKNOWN, .message = QStringLiteral("no response in the test")}));
}
SteamForge::CommunityCookiesResponse FetchCookies(SteamForge::SteamCommunityTransport& transport, const SteamForge::CommunityRequest& request) {
	QObject context;
	std::optional<SteamForge::CommunityCookiesResponse> response;
	transport.FetchCookies(request, context, [&response](SteamForge::CommunityCookiesResponse receivedResponse) {
		response = std::move(receivedResponse);
	});
	QDeadlineTimer deadline(RESPONSE_TIMEOUT_MILLISECONDS);
	while (!response.has_value() && !deadline.hasExpired()) {
		QTest::qWait(1);
	}
	return response.value_or(std::unexpected(SteamForge::WebApiError{.kind = SteamForge::WebApiErrorKind::TIMEOUT, .httpStatus = 0, .steamResult = SteamForge::SteamResult::UNKNOWN, .message = QStringLiteral("no response in the test")}));
}
} // namespace

class CommunityTransportTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ReadsTheCookiesOfARedirectAndSendsEarlierCookies() {
		const LocalHttpServer server(ELIGIBILITY_RESPONSE);
		SteamForge::SteamCommunityTransport transport(server.Origin());
		const SteamForge::CommunityRequest request{
			.path = QStringLiteral("market/eligibilitycheck/"),
			.httpMethod = SteamForge::WebApiHttpMethod::GET,
			.parameters = {{QStringLiteral("goto"), QStringLiteral("/market/")}},
			.session = Session(FIRST_STEAM_ID, QStringLiteral("header.payload.signature")),
			.extraCookies = {QNetworkCookie(QByteArray("earlierCookie"), QByteArray("earlierValue"))},
		};
		const SteamForge::CommunityCookiesResponse response = FetchCookies(transport, request);

		QVERIFY(response.has_value());
		const auto cookie = std::ranges::find(*response, QByteArray("webTradeEligibility"), &QNetworkCookie::name);
		QVERIFY(cookie != response->end());
		QCOMPARE(cookie->value(), QByteArray("%7B%22allowed%22%3A1%7D"));
		const CapturedRequest& captured = server.CapturedRequests().front();
		QCOMPARE(captured.requestLine, QByteArray("GET /market/eligibilitycheck/?goto=%2Fmarket%2F HTTP/1.1"));
		QVERIFY(captured.headers.value("cookie").contains("earlierCookie=earlierValue"));
	}

	void SendsTheSessionCookiesAndAnEncodedQuery() {
		const LocalHttpServer server(OK_RESPONSE);
		SteamForge::SteamCommunityTransport transport(server.Origin());
		const SteamForge::CommunityRequest request{
			.path = QStringLiteral("mobileconf/getlist"),
			.httpMethod = SteamForge::WebApiHttpMethod::GET,
			.parameters = {{QStringLiteral("p"), QStringLiteral("android:1")}, {QStringLiteral("k"), QStringLiteral("+/8=")}},
			.session = Session(FIRST_STEAM_ID, QStringLiteral("header.payload.signature")),
			.extraCookies = {},
		};
		const SteamForge::WebApiResponse response = Send(transport, request);

		QVERIFY(response.has_value());
		QCOMPARE(*response, JSON_BODY.toByteArray());
		const CapturedRequest& captured = server.CapturedRequests().front();
		QCOMPARE(captured.requestLine, QByteArray("GET /mobileconf/getlist?p=android%3A1&k=%2B%2F8%3D HTTP/1.1"));
		const QByteArray cookies = captured.headers.value("cookie");
		QVERIFY(cookies.contains("steamLoginSecure=76561198000000001%7C%7Cheader.payload.signature"));
		QVERIFY(cookies.contains("sessionid=0123456789abcdef01234567"));
		QVERIFY(cookies.contains("mobileClient=android"));
	}

	void SendsNoSignInWithoutASession() {
		const LocalHttpServer server(OK_RESPONSE);
		SteamForge::SteamCommunityTransport transport(server.Origin());
		const SteamForge::CommunityRequest request{
			.path = QStringLiteral("profiles/76561198000000001/"),
			.httpMethod = SteamForge::WebApiHttpMethod::GET,
			.parameters = {{QStringLiteral("xml"), QStringLiteral("1")}},
			.session = {},
			.extraCookies = {},
		};
		QVERIFY(Send(transport, request).has_value());

		const CapturedRequest& captured = server.CapturedRequests().front();
		QCOMPARE(captured.requestLine, QByteArray("GET /profiles/76561198000000001/?xml=1 HTTP/1.1"));
		const QByteArray cookies = captured.headers.value("cookie");
		QVERIFY(!cookies.contains("steamLoginSecure"));
		QVERIFY(!cookies.contains("sessionid"));
		QVERIFY(cookies.contains("Steam_Language="));
	}

	void AsksForTextsInTheLanguageOfTheInterface() {
		const QLocale previousLocale;
		const LocalHttpServer server(OK_RESPONSE);
		SteamForge::SteamCommunityTransport transport(server.Origin());
		const SteamForge::CommunityRequest request{
			.path = QStringLiteral("mobileconf/getlist"),
			.httpMethod = SteamForge::WebApiHttpMethod::GET,
			.parameters = {},
			.session = Session(FIRST_STEAM_ID, QStringLiteral("token")),
			.extraCookies = {},
		};
		QLocale::setDefault(QLocale(QLocale::Russian));
		QVERIFY(Send(transport, request).has_value());
		QLocale::setDefault(QLocale(QLocale::English));
		QVERIFY(Send(transport, request).has_value());
		QLocale::setDefault(previousLocale);

		QCOMPARE(server.CapturedRequests().size(), std::size_t{2});
		QVERIFY(server.CapturedRequests().front().headers.value("cookie").contains("Steam_Language=russian"));
		QVERIFY(server.CapturedRequests().back().headers.value("cookie").contains("Steam_Language=english"));
	}

	void PostsAFormBody() {
		const LocalHttpServer server(OK_RESPONSE);
		SteamForge::SteamCommunityTransport transport(server.Origin());
		const SteamForge::CommunityRequest request{
			.path = QStringLiteral("mobileconf/multiajaxop"),
			.httpMethod = SteamForge::WebApiHttpMethod::POST,
			.parameters = {{QStringLiteral("op"), QStringLiteral("allow")}, {QStringLiteral("cid[]"), QStringLiteral("1")}, {QStringLiteral("cid[]"), QStringLiteral("2")}},
			.session = Session(FIRST_STEAM_ID, QStringLiteral("token")),
			.extraCookies = {},
		};
		const SteamForge::WebApiResponse response = Send(transport, request);

		QVERIFY(response.has_value());
		const CapturedRequest& captured = server.CapturedRequests().front();
		QCOMPARE(captured.requestLine, QByteArray("POST /mobileconf/multiajaxop HTTP/1.1"));
		QVERIFY(captured.headers.value("content-type").startsWith("application/x-www-form-urlencoded"));
		QCOMPARE(captured.body, QByteArray("op=allow&cid%5B%5D=1&cid%5B%5D=2"));
	}

	void KeepsTheCookiesOfOneAccountAwayFromAnother() {
		const LocalHttpServer server(COOKIE_SETTING_RESPONSE);
		SteamForge::SteamCommunityTransport transport(server.Origin());
		const SteamForge::CommunityRequest firstAccountRequest{
			.path = QStringLiteral("mobileconf/getlist"),
			.httpMethod = SteamForge::WebApiHttpMethod::GET,
			.parameters = {},
			.session = Session(FIRST_STEAM_ID, QStringLiteral("first")),
			.extraCookies = {},
		};
		SteamForge::CommunityRequest secondAccountRequest = firstAccountRequest;
		secondAccountRequest.session = Session(SECOND_STEAM_ID, QStringLiteral("second"));

		QVERIFY(Send(transport, firstAccountRequest).has_value());
		QVERIFY(Send(transport, secondAccountRequest).has_value());

		QCOMPARE(server.CapturedRequests().size(), std::size_t{2});
		const QByteArray secondCookies = server.CapturedRequests().back().headers.value("cookie");
		QVERIFY(secondCookies.contains("steamLoginSecure=76561198000000002%7C%7Csecond"));
		QVERIFY(!secondCookies.contains("foreign"));
		QVERIFY(!secondCookies.contains("first"));
	}

	void DoesNotFollowARedirect() {
		const LocalHttpServer server(REDIRECT_RESPONSE);
		SteamForge::SteamCommunityTransport transport(server.Origin());
		const SteamForge::CommunityRequest request{
			.path = QStringLiteral("mobileconf/getlist"),
			.httpMethod = SteamForge::WebApiHttpMethod::GET,
			.parameters = {},
			.session = Session(FIRST_STEAM_ID, QStringLiteral("token")),
			.extraCookies = {},
		};
		const SteamForge::WebApiResponse response = Send(transport, request);

		QVERIFY(!response.has_value());
		QCOMPARE(response.error().kind, SteamForge::WebApiErrorKind::HTTP_STATUS);
		QCOMPARE(response.error().httpStatus, FOUND_STATUS);
		QCOMPARE(server.CapturedRequests().size(), std::size_t{1});
	}

	void CreatesDifferentSessionIds() {
		const QString firstSessionId = SteamForge::CreateCommunitySessionId();
		QCOMPARE(firstSessionId.size(), 24);
		QVERIFY(QByteArray::fromHex(firstSessionId.toLatin1()).toHex() == firstSessionId.toLatin1());
		QVERIFY(firstSessionId != SteamForge::CreateCommunitySessionId());
	}
};

QTEST_GUILESS_MAIN(CommunityTransportTest)

#include "community_transport_test.moc"
