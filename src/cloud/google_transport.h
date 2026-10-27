#pragma once

namespace SteamForge {

enum class GoogleHttpMethod {
	GET,
	POST,
	DELETE_RESOURCE
};

struct GoogleRequest {
	GoogleHttpMethod method = GoogleHttpMethod::GET;
	QUrl url;
	QByteArray contentType;
	QByteArray body;
	// Sent as a bearer token; empty for the sign-in endpoints.
	QString accessToken;
};

enum class GoogleErrorKind {
	NETWORK,
	TIMEOUT,
	HTTP_STATUS,
	RESPONSE_TOO_LARGE,
	MALFORMED_RESPONSE,
	// The access token expired or was revoked; a fresh one may still be issued.
	UNAUTHORIZED,
	// The refresh token no longer works: the access was revoked or expired, so the user signs in again.
	SIGN_IN_REQUIRED
};

struct GoogleError {
	GoogleErrorKind kind = GoogleErrorKind::NETWORK;
	int httpStatus = 0;
	// The error code Google returns, such as invalid_grant; never contains request data.
	QString reason;
};

using GoogleResponse = std::expected<QByteArray, GoogleError>;
using GoogleResponseHandler = std::function<void(GoogleResponse)>;

template<typename Result>
using GoogleHandler = std::function<void(std::expected<Result, GoogleError>)>;

// Network boundary of the Google sign-in and Drive requests; tests replace it with prepared responses.
class GoogleTransport {
public:
	GoogleTransport() = default;
	virtual ~GoogleTransport() = default;
	Q_DISABLE_COPY_MOVE(GoogleTransport)

	// The handler runs on the event loop and is dropped when the context object is destroyed first.
	virtual void Send(const GoogleRequest& request, QObject& context, GoogleResponseHandler handler) = 0;
};

class NetworkGoogleTransport final : public QObject, public GoogleTransport {
	Q_OBJECT

public:
	explicit NetworkGoogleTransport(QObject* parent = nullptr);

	void Send(const GoogleRequest& request, QObject& context, GoogleResponseHandler handler) override;

private:
	QNetworkAccessManager networkAccessManager;
};

} // namespace SteamForge
