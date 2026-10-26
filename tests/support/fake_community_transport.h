#pragma once

#include "steam/community_transport.h"

// Answers every path from its own queue of prepared responses, asynchronously like the real transport.
class FakeCommunityTransport final : public SteamForge::CommunityTransport {
public:
	void AddResponse(const QString& path, SteamForge::WebApiResponse response) {
		responses[path].append(std::move(response));
	}

	void AddJson(const QString& path, const QJsonObject& body) {
		AddResponse(path, QJsonDocument(body).toJson(QJsonDocument::Compact));
	}

	void AddFailure(const QString& path, SteamForge::WebApiErrorKind kind) {
		AddResponse(path, std::unexpected(SteamForge::WebApiError{
							  .kind = kind,
							  .httpStatus = 0,
							  .steamResult = SteamForge::SteamResult::UNKNOWN,
							  .message = {},
						  }));
	}

	void AddCookies(const QString& path, SteamForge::CommunityCookiesResponse response) {
		cookieResponses[path].append(std::move(response));
	}

	void FetchCookies(const SteamForge::CommunityRequest& request, QObject& context, SteamForge::CommunityCookiesHandler handler) override {
		sentRequests.push_back(request);
		QList<SteamForge::CommunityCookiesResponse>& queue = cookieResponses[request.path];
		SteamForge::CommunityCookiesResponse response = std::unexpected(SteamForge::WebApiError{
			.kind = SteamForge::WebApiErrorKind::NETWORK,
			.httpStatus = 0,
			.steamResult = SteamForge::SteamResult::UNKNOWN,
			.message = QStringLiteral("No prepared cookies for ") + request.path,
		});
		if (!queue.isEmpty()) {
			response = queue.takeFirst();
		}
		QTimer::singleShot(0, &context, [handler = std::move(handler), response = std::move(response)]() {
			handler(response);
		});
	}

	void Send(const SteamForge::CommunityRequest& request, QObject& context, SteamForge::WebApiResponseHandler handler) override {
		sentRequests.push_back(request);
		QList<SteamForge::WebApiResponse>& queue = responses[request.path];
		SteamForge::WebApiResponse response = std::unexpected(SteamForge::WebApiError{
			.kind = SteamForge::WebApiErrorKind::NETWORK,
			.httpStatus = 0,
			.steamResult = SteamForge::SteamResult::UNKNOWN,
			.message = QStringLiteral("No prepared response for ") + request.path,
		});
		if (!queue.isEmpty()) {
			response = queue.takeFirst();
		}
		QTimer::singleShot(0, &context, [handler = std::move(handler), response = std::move(response)]() {
			handler(response);
		});
	}

	[[nodiscard]] std::vector<SteamForge::CommunityRequest> SentTo(const QString& path) const {
		std::vector<SteamForge::CommunityRequest> matchingRequests;
		std::ranges::copy_if(sentRequests, std::back_inserter(matchingRequests), [&path](const SteamForge::CommunityRequest& sentRequest) {
			return sentRequest.path == path;
		});
		return matchingRequests;
	}

	[[nodiscard]] std::size_t SentCount() const {
		return sentRequests.size();
	}

private:
	QHash<QString, QList<SteamForge::WebApiResponse>> responses;
	QHash<QString, QList<SteamForge::CommunityCookiesResponse>> cookieResponses;
	std::vector<SteamForge::CommunityRequest> sentRequests;
};

// The value of the first parameter with this name, or an empty string.
[[nodiscard]] inline QString ParameterValue(const SteamForge::RequestParameters& parameters, QStringView name) {
	const auto parameterIterator = std::ranges::find_if(parameters, [name](const std::pair<QString, QString>& parameter) {
		return parameter.first == name;
	});
	if (parameterIterator == parameters.end()) {
		return {};
	}
	return parameterIterator->second;
}

[[nodiscard]] inline QStringList ParameterValues(const SteamForge::RequestParameters& parameters, QStringView name) {
	QStringList values;
	for (const auto& [parameterName, value] : parameters) {
		if (parameterName == name) {
			values.append(value);
		}
	}
	return values;
}
