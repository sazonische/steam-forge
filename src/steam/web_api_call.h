#pragma once

#include "steam/web_api_transport.h"

namespace SteamForge {

[[nodiscard]] inline WebApiError MalformedWebApiResponse() {
	return WebApiError{
		.kind = WebApiErrorKind::MALFORMED_RESPONSE,
		.httpStatus = 0,
		.steamResult = SteamResult::UNKNOWN,
		.message = {},
	};
}

template<typename Message>
[[nodiscard]] QByteArray SerializeMessage(const Message& message) {
	std::string bytes;
	message.SerializeToString(&bytes);
	return QByteArray::fromStdString(bytes);
}

// Parses the protobuf response and converts it; transport and parse errors reach the handler unchanged.
template<typename ResponseMessage, typename Result, typename Convert>
void CallWebApi(WebApiTransport& transport, const WebApiRequest& request, QObject& context, WebApiHandler<Result> handler, Convert convert) {
	transport.Send(request, context, [resultHandler = std::move(handler), convertResponse = std::move(convert)](WebApiResponse response) {
		if (!response.has_value()) {
			resultHandler(std::unexpected(response.error()));
			return;
		}
		ResponseMessage message;
		if (!message.ParseFromArray(response->constData(), static_cast<int>(response->size()))) {
			resultHandler(std::unexpected(MalformedWebApiResponse()));
			return;
		}
		resultHandler(convertResponse(message));
	});
}

inline void CallWebApiWithoutResult(WebApiTransport& transport, const WebApiRequest& request, QObject& context, WebApiHandler<void> handler) {
	transport.Send(request, context, [resultHandler = std::move(handler)](const WebApiResponse& response) {
		if (!response.has_value()) {
			resultHandler(std::unexpected(response.error()));
			return;
		}
		resultHandler({});
	});
}

[[nodiscard]] inline QString FromUtf8(const std::string& text) {
	return QString::fromStdString(text);
}

} // namespace SteamForge
