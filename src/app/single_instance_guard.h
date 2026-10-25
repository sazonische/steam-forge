#pragma once

namespace SteamForge {

enum class InstanceAcquisition {
	ACQUIRED,
	ACTIVATED_RUNNING_INSTANCE,
	UNAVAILABLE
};

class SingleInstanceGuard final : public QObject {
	Q_OBJECT

public:
	explicit SingleInstanceGuard(QObject* parent = nullptr);

	[[nodiscard]] InstanceAcquisition Acquire();

Q_SIGNALS:
	void ActivationRequested();

private:
	void AcceptActivationRequests();

	QString serverName;
	QLocalServer activationServer;
};

} // namespace SteamForge
