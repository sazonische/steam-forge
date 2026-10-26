#pragma once

#include "steam/web_api_transport.h"
#include "support/synthetic_tokens.h"

#include "steam_authentication.pb.h"

// Answers every method from its own queue of prepared responses, asynchronously like the real transport.
class FakeWebApiTransport final : public SteamForge::WebApiTransport {
public:
	struct SentRequest {
		QString methodName;
		SteamForge::WebApiHttpMethod httpMethod = SteamForge::WebApiHttpMethod::POST;
		QByteArray protobufBody;
		QString accessToken;
		SteamForge::RequestParameters jsonParameters;
	};

	void AddResponse(QLatin1StringView methodName, SteamForge::WebApiResponse response) {
		responses[QString(methodName)].append(std::move(response));
	}

	template<typename Message>
	void AddMessage(QLatin1StringView methodName, const Message& message) {
		std::string bytes;
		message.SerializeToString(&bytes);
		AddResponse(methodName, QByteArray::fromStdString(bytes));
	}

	void AddSteamError(QLatin1StringView methodName, SteamForge::SteamResult steamResult) {
		AddResponse(methodName, std::unexpected(SteamForge::WebApiError{
									.kind = SteamForge::WebApiErrorKind::STEAM_RESULT,
									.httpStatus = 200,
									.steamResult = steamResult,
									.message = {},
								}));
	}

	void AddHttpError(QLatin1StringView methodName, int httpStatus) {
		AddResponse(methodName, std::unexpected(SteamForge::WebApiError{
									.kind = SteamForge::WebApiErrorKind::HTTP_STATUS,
									.httpStatus = httpStatus,
									.steamResult = SteamForge::SteamResult::UNKNOWN,
									.message = {},
								}));
	}

	void Send(const SteamForge::WebApiRequest& request, QObject& context, SteamForge::WebApiResponseHandler handler) override {
		const QString methodName(request.methodName);
		sentRequests.push_back({
			.methodName = methodName,
			.httpMethod = request.httpMethod,
			.protobufBody = request.protobufBody,
			.accessToken = request.accessToken,
			.jsonParameters = request.jsonParameters,
		});
		QList<SteamForge::WebApiResponse>& queue = responses[methodName];
		SteamForge::WebApiResponse response = std::unexpected(SteamForge::WebApiError{
			.kind = SteamForge::WebApiErrorKind::NETWORK,
			.httpStatus = 0,
			.steamResult = SteamForge::SteamResult::UNKNOWN,
			.message = QStringLiteral("No prepared response for ") + methodName,
		});
		if (!queue.isEmpty()) {
			response = queue.takeFirst();
		}
		QTimer::singleShot(0, &context, [handler = std::move(handler), response = std::move(response)]() {
			handler(response);
		});
	}

	[[nodiscard]] std::vector<SentRequest> SentTo(QLatin1StringView methodName) const {
		std::vector<SentRequest> matchingRequests;
		std::ranges::copy_if(sentRequests, std::back_inserter(matchingRequests), [methodName](const SentRequest& sentRequest) {
			return sentRequest.methodName == methodName;
		});
		return matchingRequests;
	}

	[[nodiscard]] std::size_t SentCount() const {
		return sentRequests.size();
	}

private:
	QHash<QString, QList<SteamForge::WebApiResponse>> responses;
	std::vector<SentRequest> sentRequests;
};
