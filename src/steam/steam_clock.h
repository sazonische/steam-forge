#pragma once

namespace SteamForge {

enum class SteamClockState {
	NOT_SYNCHRONIZED,
	SYNCHRONIZED,
	SYNCHRONIZATION_FAILED
};

struct SteamTimeResponse {
	std::chrono::sys_seconds serverTime;
	std::optional<std::chrono::seconds> skewTolerance;
	std::optional<std::chrono::seconds> probeInterval;
	std::optional<std::chrono::seconds> adjustedTimeProbeInterval;
	std::optional<std::chrono::seconds> minimumProbeInterval;
	std::optional<std::chrono::seconds> retryInterval;
};

[[nodiscard]] std::optional<SteamTimeResponse> ParseQueryTimeResponse(const QByteArray& responseBody);

[[nodiscard]] std::chrono::seconds NextProbeDelay(const SteamTimeResponse& response, std::chrono::milliseconds measuredOffset);

class SteamClock final : public QObject {
	Q_OBJECT

public:
	explicit SteamClock(QObject* parent = nullptr);

	[[nodiscard]] std::chrono::sys_time<std::chrono::milliseconds> Now() const;
	[[nodiscard]] std::chrono::milliseconds Offset() const;
	// Stays SYNCHRONIZED after a failed periodic query: the last measured offset is still the best estimate.
	[[nodiscard]] SteamClockState State() const;

	void Synchronize();

Q_SIGNALS:
	// Also emitted when a periodic query keeps the state but measures a new offset.
	void StateChanged();

private:
	void HandleReply(QNetworkReply* reply, std::chrono::system_clock::time_point requestStartedAt, std::chrono::steady_clock::time_point requestStartedMonotonic);
	void SetState(SteamClockState newState);

	QNetworkAccessManager networkAccessManager;
	QTimer synchronizationTimer;
	std::chrono::milliseconds offset{0};
	std::chrono::seconds failureRetryDelay;
	SteamClockState state = SteamClockState::NOT_SYNCHRONIZED;
	bool isRequestInFlight = false;
};

} // namespace SteamForge
