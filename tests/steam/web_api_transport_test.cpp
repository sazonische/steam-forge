#include "steam/web_api_transport.h"
#include "support/local_http_server.h"

namespace {
constexpr QByteArrayView HEADER_END{"\r\n\r\n"};
constexpr QByteArrayView LINE_END{"\r\n"};
constexpr int RESPONSE_TIMEOUT_MILLISECONDS = 10000;
constexpr int BAD_REQUEST_STATUS = 400;
constexpr QLatin1StringView SECRET_ACCESS_TOKEN{"synthetic-secret-access-token"};
constexpr QByteArrayView RESPONSE_BODY{"body"};
// Base64 of these bytes is "+/8=", so every character that needs percent-encoding is present.
constexpr QByteArrayView PROTOBUF_BODY_HEX{"fbff"};
constexpr QByteArrayView PROTOBUF_BODY_BASE64{"+/8="};

constexpr QByteArrayView OK_RESPONSE{"HTTP/1.1 200 OK\r\nx-eresult: 1\r\nContent-Length: 4\r\nConnection: close\r\n\r\nbody"};
constexpr QByteArrayView STEAM_FAILURE_RESPONSE{"HTTP/1.1 200 OK\r\nx-eresult: 5\r\nx-error_message: wrong\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};
// Classic JSON methods may answer without the x-eresult header.
constexpr QByteArrayView JSON_RESPONSE{"HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: 15\r\nConnection: close\r\n\r\n{\"response\":{}}"};
constexpr QByteArrayView BAD_REQUEST_RESPONSE{"HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"};

SteamForge::WebApiResponse Send(const QString& origin, const SteamForge::WebApiRequest& request) {
	SteamForge::SteamWebApiTransport transport(origin);
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

SteamForge::WebApiRequest Request(SteamForge::WebApiHttpMethod httpMethod, const QString& accessToken = {}) {
	return {
		.interfaceName = QLatin1StringView("Authentication"),
		.methodName = QLatin1StringView("BeginAuthSessionViaCredentials"),
		.version = 1,
		.httpMethod = httpMethod,
		.protobufBody = QByteArray::fromHex(PROTOBUF_BODY_HEX.toByteArray()),
		.accessToken = accessToken,
		.encoding = SteamForge::WebApiEncoding::PROTOBUF,
		.jsonParameters = {},
	};
}
} // namespace

class WebApiTransportTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void PostsProtobufAsMultipartWithUnquotedBoundary() {
		const LocalHttpServer server(OK_RESPONSE);
		const SteamForge::WebApiResponse response = Send(server.Origin(), Request(SteamForge::WebApiHttpMethod::POST));

		QVERIFY(response.has_value());
		QCOMPARE(*response, RESPONSE_BODY.toByteArray());
		QVERIFY(!server.CapturedRequests().empty());
		const CapturedRequest& request = server.CapturedRequests().front();
		QVERIFY(request.requestLine.startsWith("POST /IAuthenticationService/BeginAuthSessionViaCredentials/v1/ HTTP/1.1"));
		const QByteArray contentType = request.headers.value("content-type");
		const QRegularExpressionMatch boundaryMatch = QRegularExpression(QStringLiteral(R"(^multipart/form-data; boundary=([A-Za-z0-9]+)$)")).match(QString::fromLatin1(contentType));
		QVERIFY2(boundaryMatch.hasMatch(), contentType.constData());
		const QByteArray boundary = boundaryMatch.captured(1).toLatin1();
		QVERIFY(request.body.startsWith("--" + boundary + LINE_END.toByteArray()));
		QVERIFY(request.body.toLower().contains(R"(content-disposition: form-data; name="input_protobuf_encoded")"));
		QVERIFY(request.body.contains(HEADER_END.toByteArray() + PROTOBUF_BODY_BASE64.toByteArray() + LINE_END.toByteArray() + "--" + boundary + "--"));
		QCOMPARE(request.headers.value("cookie"), QByteArray("mobileClient=android; mobileClientVersion=777777 3.10.9"));
		QCOMPARE(request.headers.value("user-agent"), QByteArray("okhttp/4.9.2"));
	}

	void PostsAnEmptyMessageAsAnEmptyField() {
		const LocalHttpServer server(OK_RESPONSE);
		SteamForge::WebApiRequest request = Request(SteamForge::WebApiHttpMethod::POST);
		request.protobufBody.clear();
		const SteamForge::WebApiResponse response = Send(server.Origin(), request);

		QVERIFY(response.has_value());
		QVERIFY(!server.CapturedRequests().empty());
		QVERIFY(server.CapturedRequests().front().body.toLower().contains(R"(name="input_protobuf_encoded")"));
	}

	void PutsProtobufAndTokenIntoTheQueryOfGet() {
		const LocalHttpServer server(OK_RESPONSE);
		const SteamForge::WebApiResponse response = Send(server.Origin(), Request(SteamForge::WebApiHttpMethod::GET, QStringLiteral("a+b")));

		QVERIFY(response.has_value());
		QVERIFY(!server.CapturedRequests().empty());
		QCOMPARE(server.CapturedRequests().front().requestLine, QByteArray("GET /IAuthenticationService/BeginAuthSessionViaCredentials/v1/?access_token=a%2Bb&input_protobuf_encoded=%2B%2F8%3D&origin=SteamMobile HTTP/1.1"));
		QVERIFY(server.CapturedRequests().front().body.isEmpty());
	}

	void GetOfAnEmptyMessageStillAsksForProtobuf() {
		const LocalHttpServer server(OK_RESPONSE);
		SteamForge::WebApiRequest request = Request(SteamForge::WebApiHttpMethod::GET);
		request.protobufBody.clear();
		const SteamForge::WebApiResponse response = Send(server.Origin(), request);

		QVERIFY(response.has_value());
		QVERIFY(!server.CapturedRequests().empty());
		QCOMPARE(server.CapturedRequests().front().requestLine, QByteArray("GET /IAuthenticationService/BeginAuthSessionViaCredentials/v1/?input_protobuf_encoded=&origin=SteamMobile HTTP/1.1"));
	}

	void ReportsTheSteamResultHeader() {
		const LocalHttpServer server(STEAM_FAILURE_RESPONSE);
		const SteamForge::WebApiResponse response = Send(server.Origin(), Request(SteamForge::WebApiHttpMethod::POST));

		QVERIFY(!response.has_value());
		QCOMPARE(response.error().kind, SteamForge::WebApiErrorKind::STEAM_RESULT);
		QCOMPARE(response.error().steamResult, SteamForge::SteamResult::INVALID_PASSWORD);
		QCOMPARE(response.error().message, QStringLiteral("wrong"));
	}

	void ReportsTheHttpStatus() {
		const LocalHttpServer server(BAD_REQUEST_RESPONSE);
		const SteamForge::WebApiResponse response = Send(server.Origin(), Request(SteamForge::WebApiHttpMethod::POST));

		QVERIFY(!response.has_value());
		QCOMPARE(response.error().kind, SteamForge::WebApiErrorKind::HTTP_STATUS);
		QCOMPARE(response.error().httpStatus, BAD_REQUEST_STATUS);
	}

	void KeepsTheAccessTokenOutOfErrors() {
		const LocalHttpServer server(BAD_REQUEST_RESPONSE);
		const SteamForge::WebApiResponse httpFailure = Send(server.Origin(), Request(SteamForge::WebApiHttpMethod::GET, QString(SECRET_ACCESS_TOKEN)));
		QVERIFY(!httpFailure.has_value());
		QVERIFY(!httpFailure.error().message.contains(SECRET_ACCESS_TOKEN));

		// Nothing listens on this port, so the request fails in the network layer.
		const SteamForge::WebApiResponse networkFailure = Send(QStringLiteral("http://127.0.0.1:1"), Request(SteamForge::WebApiHttpMethod::GET, QString(SECRET_ACCESS_TOKEN)));
		QVERIFY(!networkFailure.has_value());
		QCOMPARE(networkFailure.error().kind, SteamForge::WebApiErrorKind::NETWORK);
		QVERIFY(!networkFailure.error().message.isEmpty());
		QVERIFY(!networkFailure.error().message.contains(SECRET_ACCESS_TOKEN));
		QVERIFY(!networkFailure.error().message.contains(QStringLiteral("127.0.0.1")));
	}

	void SendsAJsonMethodWithQueryParametersAndReadsItsBody() {
		const LocalHttpServer server(JSON_RESPONSE);
		SteamForge::WebApiRequest request = Request(SteamForge::WebApiHttpMethod::GET, QStringLiteral("a+b"));
		request.interfaceName = QLatin1StringView("Econ");
		request.methodName = QLatin1StringView("GetTradeOffer");
		request.encoding = SteamForge::WebApiEncoding::JSON;
		request.jsonParameters = {{QStringLiteral("tradeofferid"), QStringLiteral("6123456789")}, {QStringLiteral("get_descriptions"), QStringLiteral("1")}};
		const SteamForge::WebApiResponse response = Send(server.Origin(), request);

		QVERIFY(response.has_value());
		QCOMPARE(*response, QByteArray(R"({"response":{}})"));
		QCOMPARE(server.CapturedRequests().front().requestLine, QByteArray("GET /IEconService/GetTradeOffer/v1/?access_token=a%2Bb&tradeofferid=6123456789&get_descriptions=1 HTTP/1.1"));
	}
};

QTEST_GUILESS_MAIN(WebApiTransportTest)

#include "web_api_transport_test.moc"
