#pragma once

#include "cloud/google_transport.h"

#include <deque>
#include <map>

// Answers requests from queues of prepared responses keyed by method and path, asynchronously like the network.
class FakeGoogleTransport final : public SteamForge::GoogleTransport {
public:
	void AddResponse(SteamForge::GoogleHttpMethod method, const QString& path, SteamForge::GoogleResponse response) {
		responses[Key(method, path)].push_back(std::move(response));
	}

	void AddError(SteamForge::GoogleHttpMethod method, const QString& path, SteamForge::GoogleErrorKind kind, int httpStatus = 0, const QString& reason = {}) {
		AddResponse(method, path, std::unexpected(SteamForge::GoogleError{.kind = kind, .httpStatus = httpStatus, .reason = reason}));
	}

	void Send(const SteamForge::GoogleRequest& request, QObject& context, SteamForge::GoogleResponseHandler handler) override {
		sentRequests.push_back(request);
		std::deque<SteamForge::GoogleResponse>& queue = responses[Key(request.method, request.url.path())];
		SteamForge::GoogleResponse response = std::unexpected(SteamForge::GoogleError{.kind = SteamForge::GoogleErrorKind::NETWORK, .httpStatus = 0, .reason = QStringLiteral("no prepared response")});
		if (!queue.empty()) {
			response = std::move(queue.front());
			queue.pop_front();
		}
		QTimer::singleShot(0, &context, [preparedResponse = std::move(response), responseHandler = std::move(handler)]() {
			responseHandler(preparedResponse);
		});
	}

	[[nodiscard]] const std::vector<SteamForge::GoogleRequest>& SentRequests() const {
		return sentRequests;
	}

	[[nodiscard]] qsizetype PendingResponseCount() const {
		qsizetype pendingCount = 0;
		for (const auto& queueByRequest : responses) {
			pendingCount += static_cast<qsizetype>(queueByRequest.second.size());
		}
		return pendingCount;
	}

private:
	static QString Key(SteamForge::GoogleHttpMethod method, const QString& path) {
		return QString::number(static_cast<int>(method)) + u' ' + path;
	}

	// Standard containers: Qt ones would ask for == on the responses, which GoogleError does not have.
	std::map<QString, std::deque<SteamForge::GoogleResponse>> responses;
	std::vector<SteamForge::GoogleRequest> sentRequests;
};
