#include "steam/web_api_transport.h"

#include "steam/steam_http.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView STEAM_API_ORIGIN{"https://api.steampowered.com"};
	constexpr QLatin1StringView PATH_PATTERN{"/I%1Service/%2/v%3/"};
	constexpr QLatin1StringView PROTOBUF_FIELD_NAME{"input_protobuf_encoded"};
	constexpr QLatin1StringView ACCESS_TOKEN_PARAMETER{"access_token"};
	constexpr QLatin1StringView MOBILE_ORIGIN_QUERY{"origin=SteamMobile"};
	constexpr QLatin1StringView FORM_DATA_DISPOSITION{"form-data; name=\"%1\""};
	constexpr QByteArrayView MULTIPART_CONTENT_TYPE_PREFIX{"multipart/form-data; boundary="};
	constexpr QByteArrayView MULTIPART_BOUNDARY_PREFIX{"SteamForge"};
	constexpr QByteArrayView STEAM_RESULT_HEADER{"x-eresult"};
	constexpr QByteArrayView ERROR_MESSAGE_HEADER{"x-error_message"};

	QString PercentEncoded(const QByteArray& value) {
		return QString::fromLatin1(QUrl::toPercentEncoding(QString::fromLatin1(value)));
	}

	QUrl RequestUrl(const QString& origin, const WebApiRequest& request) {
		QUrl url(origin + QString(PATH_PATTERN).arg(request.interfaceName, request.methodName).arg(request.version));
		// Built by hand: QUrlQuery leaves '+' of Base64 unencoded, and the server would read it as a space.
		QStringList queryItems;
		if (!request.accessToken.isEmpty()) {
			queryItems.append(ACCESS_TOKEN_PARAMETER + QLatin1Char('=') + PercentEncoded(request.accessToken.toLatin1()));
		}
		if (request.encoding == WebApiEncoding::JSON) {
			queryItems.append(QString::fromLatin1(EncodeParameters(request.jsonParameters)));
		} else if (request.httpMethod == WebApiHttpMethod::GET) {
			// Present even for an empty message: without it Steam answers in JSON instead of protobuf.
			queryItems.append(PROTOBUF_FIELD_NAME + QLatin1Char('=') + PercentEncoded(request.protobufBody.toBase64()));
			queryItems.append(MOBILE_ORIGIN_QUERY);
		}
		if (!queryItems.isEmpty()) {
			url.setQuery(queryItems.join(QLatin1Char('&')), QUrl::StrictMode);
		}
		return url;
	}

	WebApiResponse ReadResponse(QNetworkReply& reply, WebApiEncoding encoding) {
		if (std::optional<WebApiError> failure = ReplyFailure(reply); failure.has_value()) {
			return std::unexpected(std::move(*failure));
		}
		const int httpStatus = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

		bool isResultValid = false;
		const int resultValue = reply.rawHeader(STEAM_RESULT_HEADER.toByteArray()).toInt(&isResultValid);
		// Classic JSON methods do not always send the result header; their body tells the outcome.
		if (!isResultValid && encoding == WebApiEncoding::JSON) {
			return reply.readAll();
		}
		if (!isResultValid) {
			return std::unexpected(WebApiError{.kind = WebApiErrorKind::MALFORMED_RESPONSE, .httpStatus = httpStatus, .steamResult = SteamResult::UNKNOWN, .message = {}});
		}
		const auto steamResult = static_cast<SteamResult>(resultValue);
		if (steamResult != SteamResult::OK) {
			return std::unexpected(WebApiError{
				.kind = WebApiErrorKind::STEAM_RESULT,
				.httpStatus = httpStatus,
				.steamResult = steamResult,
				.message = QString::fromUtf8(reply.rawHeader(ERROR_MESSAGE_HEADER.toByteArray())),
			});
		}
		return reply.readAll();
	}
} // namespace

SteamWebApiTransport::SteamWebApiTransport(QObject* parent) :
	SteamWebApiTransport(STEAM_API_ORIGIN, parent) {
}

SteamWebApiTransport::SteamWebApiTransport(QString apiOrigin, QObject* parent) :
	QObject(parent),
	origin(std::move(apiOrigin)) {
}

void SteamWebApiTransport::Send(const WebApiRequest& request, QObject& context, WebApiResponseHandler handler) {
	QNetworkRequest networkRequest(RequestUrl(origin, request));
	ApplyMobileClientHeaders(networkRequest);

	QNetworkReply* reply = nullptr;
	if (request.httpMethod == WebApiHttpMethod::GET) {
		reply = networkAccessManager.get(networkRequest);
	} else {
		auto* multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
		// Steam answers HTTP 400 to the quoted boundary QHttpMultiPart announces by default; a hex boundary needs no quotes.
		multipart->setBoundary(MULTIPART_BOUNDARY_PREFIX.toByteArray() + QUuid::createUuid().toByteArray(QUuid::Id128));
		networkRequest.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray(MULTIPART_CONTENT_TYPE_PREFIX.toByteArray() + multipart->boundary()));
		// An empty message is still sent as an empty field: Steam rejects a multipart body without parts.
		QHttpPart protobufPart;
		protobufPart.setHeader(QNetworkRequest::ContentDispositionHeader, QString(FORM_DATA_DISPOSITION).arg(PROTOBUF_FIELD_NAME));
		protobufPart.setBody(request.protobufBody.toBase64());
		multipart->append(protobufPart);
		reply = networkAccessManager.post(networkRequest, multipart);
		multipart->setParent(reply);
	}

	WatchReply(reply, context, [responseHandler = std::move(handler), encoding = request.encoding](QNetworkReply& finishedReply) {
		responseHandler(ReadResponse(finishedReply, encoding));
	});
}

} // namespace SteamForge
