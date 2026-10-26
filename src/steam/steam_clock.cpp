#include "steam/steam_clock.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView QUERY_TIME_URL{"https://api.steampowered.com/ITwoFactorService/QueryTime/v1/"};
	constexpr QLatin1StringView FORM_CONTENT_TYPE{"application/x-www-form-urlencoded"};
	constexpr QLatin1StringView RESPONSE_KEY{"response"};
	constexpr QLatin1StringView SERVER_TIME_KEY{"server_time"};
	constexpr QLatin1StringView SKEW_TOLERANCE_KEY{"skew_tolerance_seconds"};
	constexpr QLatin1StringView PROBE_FREQUENCY_KEY{"probe_frequency_seconds"};
	constexpr QLatin1StringView ADJUSTED_TIME_PROBE_FREQUENCY_KEY{"adjusted_time_probe_frequency_seconds"};
	constexpr QLatin1StringView HINT_PROBE_FREQUENCY_KEY{"hint_probe_frequency_seconds"};
	constexpr QLatin1StringView TRY_AGAIN_KEY{"try_again_seconds"};

	constexpr std::chrono::milliseconds REQUEST_TIMEOUT{std::chrono::seconds(15)};
	constexpr std::chrono::seconds DEFAULT_PROBE_INTERVAL{std::chrono::hours(1)};
	constexpr std::chrono::seconds DEFAULT_RETRY_INTERVAL{std::chrono::minutes(5)};
	constexpr std::chrono::seconds SHORTEST_PROBE_INTERVAL{std::chrono::minutes(1)};
	constexpr std::chrono::seconds LONGEST_PROBE_INTERVAL{std::chrono::hours(24)};
	// Far beyond any real server time; a garbage value must not shift codes by decades.
	constexpr quint64 LATEST_PLAUSIBLE_SERVER_TIME = quint64{1} << 40;
	// The reply carries whole seconds; the true server time lies anywhere within that second.
	constexpr std::chrono::milliseconds SERVER_SECOND_MIDPOINT{500};

	std::optional<quint64> ReadUnsigned(const QJsonValue& value) {
		// The Steam Web API writes 64-bit integers as strings and 32-bit integers as numbers.
		if (value.isString()) {
			bool isConverted = false;
			const quint64 number = value.toString().toULongLong(&isConverted);
			if (!isConverted) {
				return std::nullopt;
			}
			return number;
		}
		if (value.isDouble()) {
			const qint64 number = value.toInteger(-1);
			if (number < 0) {
				return std::nullopt;
			}
			return static_cast<quint64>(number);
		}
		return std::nullopt;
	}

	std::optional<std::chrono::seconds> ReadSeconds(const QJsonObject& object, QLatin1StringView key) {
		const std::optional<quint64> value = ReadUnsigned(object.value(key));
		if (!value.has_value() || *value > static_cast<quint64>(LONGEST_PROBE_INTERVAL.count())) {
			return std::nullopt;
		}
		return std::chrono::seconds(static_cast<std::chrono::seconds::rep>(*value));
	}
} // namespace

std::optional<SteamTimeResponse> ParseQueryTimeResponse(const QByteArray& responseBody) {
	const QJsonObject response = QJsonDocument::fromJson(responseBody).object().value(RESPONSE_KEY).toObject();
	const std::optional<quint64> serverTime = ReadUnsigned(response.value(SERVER_TIME_KEY));
	if (!serverTime.has_value() || *serverTime == 0 || *serverTime > LATEST_PLAUSIBLE_SERVER_TIME) {
		return std::nullopt;
	}
	return SteamTimeResponse{
		.serverTime = std::chrono::sys_seconds(std::chrono::seconds(static_cast<std::chrono::seconds::rep>(*serverTime))),
		.skewTolerance = ReadSeconds(response, SKEW_TOLERANCE_KEY),
		.probeInterval = ReadSeconds(response, PROBE_FREQUENCY_KEY),
		.adjustedTimeProbeInterval = ReadSeconds(response, ADJUSTED_TIME_PROBE_FREQUENCY_KEY),
		.minimumProbeInterval = ReadSeconds(response, HINT_PROBE_FREQUENCY_KEY),
		.retryInterval = ReadSeconds(response, TRY_AGAIN_KEY),
	};
}

std::chrono::seconds NextProbeDelay(const SteamTimeResponse& response, std::chrono::milliseconds measuredOffset) {
	const std::chrono::seconds skewTolerance = response.skewTolerance.value_or(std::chrono::seconds::zero());
	const bool isLocalClockAccurate = std::chrono::abs(measuredOffset) <= skewTolerance;
	const std::optional<std::chrono::seconds> suggestedInterval = isLocalClockAccurate ? response.probeInterval : response.adjustedTimeProbeInterval;
	std::chrono::seconds delay = suggestedInterval.value_or(DEFAULT_PROBE_INTERVAL);
	if (response.minimumProbeInterval.has_value()) {
		delay = std::max(delay, *response.minimumProbeInterval);
	}
	return std::clamp(delay, SHORTEST_PROBE_INTERVAL, LONGEST_PROBE_INTERVAL);
}

SteamClock::SteamClock(QObject* parent) :
	QObject(parent),
	failureRetryDelay(DEFAULT_RETRY_INTERVAL) {
	synchronizationTimer.setSingleShot(true);
	connect(&synchronizationTimer, &QTimer::timeout, this, &SteamClock::Synchronize);
}

std::chrono::sys_time<std::chrono::milliseconds> SteamClock::Now() const {
	return std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now()) + offset;
}

std::chrono::milliseconds SteamClock::Offset() const {
	return offset;
}

SteamClockState SteamClock::State() const {
	return state;
}

void SteamClock::Synchronize() {
	if (isRequestInFlight) {
		return;
	}
	synchronizationTimer.stop();

	QNetworkRequest request{QUrl(QString(QUERY_TIME_URL))};
	request.setHeader(QNetworkRequest::ContentTypeHeader, QString(FORM_CONTENT_TYPE));
	request.setTransferTimeout(REQUEST_TIMEOUT);

	const std::chrono::system_clock::time_point requestStartedAt = std::chrono::system_clock::now();
	const std::chrono::steady_clock::time_point requestStartedMonotonic = std::chrono::steady_clock::now();
	isRequestInFlight = true;
	QNetworkReply* reply = networkAccessManager.post(request, QByteArray());
	connect(reply, &QNetworkReply::finished, this, [this, reply, requestStartedAt, requestStartedMonotonic]() {
		HandleReply(reply, requestStartedAt, requestStartedMonotonic);
	});
}

void SteamClock::HandleReply(QNetworkReply* reply, std::chrono::system_clock::time_point requestStartedAt, std::chrono::steady_clock::time_point requestStartedMonotonic) {
	reply->deleteLater();
	isRequestInFlight = false;
	const std::chrono::steady_clock::time_point requestFinishedMonotonic = std::chrono::steady_clock::now();

	std::optional<SteamTimeResponse> response;
	if (reply->error() == QNetworkReply::NoError) {
		response = ParseQueryTimeResponse(reply->readAll());
	}
	if (!response.has_value()) {
		qWarning("Steam time synchronization failed: %s", qUtf8Printable(reply->errorString()));
		if (state != SteamClockState::SYNCHRONIZED) {
			SetState(SteamClockState::SYNCHRONIZATION_FAILED);
		}
		synchronizationTimer.start(failureRetryDelay);
		return;
	}

	// The server answered somewhere between sending and receiving; the midpoint is the best estimate.
	const auto roundTrip = std::chrono::duration_cast<std::chrono::milliseconds>(requestFinishedMonotonic - requestStartedMonotonic);
	const auto localMidpoint = std::chrono::time_point_cast<std::chrono::milliseconds>(requestStartedAt) + roundTrip / 2;
	offset = (response->serverTime + SERVER_SECOND_MIDPOINT) - localMidpoint;
	failureRetryDelay = std::clamp(response->retryInterval.value_or(DEFAULT_RETRY_INTERVAL), SHORTEST_PROBE_INTERVAL, LONGEST_PROBE_INTERVAL);
	state = SteamClockState::SYNCHRONIZED;
	Q_EMIT StateChanged();
	synchronizationTimer.start(NextProbeDelay(*response, offset));
}

void SteamClock::SetState(SteamClockState newState) {
	if (state == newState) {
		return;
	}
	state = newState;
	Q_EMIT StateChanged();
}

} // namespace SteamForge
