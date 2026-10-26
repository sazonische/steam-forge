#pragma once

#include "steam/steam_result.h"

namespace SteamForge {

enum class WebApiHttpMethod {
	GET,
	POST
};

enum class WebApiEncoding {
	PROTOBUF,
	// Classic methods such as IEconService/GetTradeOffer: query parameters in, JSON out.
	JSON
};

using RequestParameters = QList<std::pair<QString, QString>>;

struct WebApiRequest {
	QLatin1StringView interfaceName;
	QLatin1StringView methodName;
	int version = 1;
	WebApiHttpMethod httpMethod = WebApiHttpMethod::POST;
	QByteArray protobufBody;
	QString accessToken;
	WebApiEncoding encoding = WebApiEncoding::PROTOBUF;
	RequestParameters jsonParameters;
};

enum class WebApiErrorKind {
	NETWORK,
	TIMEOUT,
	HTTP_STATUS,
	STEAM_RESULT,
	MALFORMED_RESPONSE,
	RESPONSE_TOO_LARGE,
	// EResult OK, but the response body reports that Steam did nothing.
	UNSUCCESSFUL_RESPONSE,
	// Steam Community did not accept the sign-in in the cookies ("needauth").
	NEEDS_AUTHENTICATION
};

struct WebApiError {
	WebApiErrorKind kind = WebApiErrorKind::NETWORK;
	int httpStatus = 0;
	SteamResult steamResult = SteamResult::UNKNOWN;
	// Text from the network layer or Steam; never contains request data.
	QString message;
};

using WebApiResponse = std::expected<QByteArray, WebApiError>;
using WebApiResponseHandler = std::function<void(WebApiResponse)>;

template<typename Result>
using WebApiHandler = std::function<void(std::expected<Result, WebApiError>)>;

// Network boundary of IAuthenticationService calls; tests replace it with recorded responses.
class WebApiTransport {
public:
	WebApiTransport() = default;
	virtual ~WebApiTransport() = default;
	Q_DISABLE_COPY_MOVE(WebApiTransport)

	// The handler runs on the event loop and is dropped when the context object is destroyed first.
	virtual void Send(const WebApiRequest& request, QObject& context, WebApiResponseHandler handler) = 0;
};

// Requests over HTTPS to api.steampowered.com as the Steam mobile application sends them.
class SteamWebApiTransport final : public QObject, public WebApiTransport {
	Q_OBJECT

public:
	explicit SteamWebApiTransport(QObject* parent = nullptr);
	// Tests point the transport at a local server instead of api.steampowered.com.
	explicit SteamWebApiTransport(QString apiOrigin, QObject* parent = nullptr);

	void Send(const WebApiRequest& request, QObject& context, WebApiResponseHandler handler) override;

private:
	QString origin;
	QNetworkAccessManager networkAccessManager;
};

} // namespace SteamForge
