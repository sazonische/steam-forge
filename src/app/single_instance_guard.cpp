#include "app/single_instance_guard.h"

#include "common/constants.h"

namespace SteamForge {

namespace {
	constexpr auto FIRST_ACTIVATION_TIMEOUT = std::chrono::milliseconds(500);
	constexpr auto RACING_STARTUP_ACTIVATION_TIMEOUT = std::chrono::milliseconds(1000);
	constexpr qsizetype USER_KEY_HEX_LENGTH = 16;
	constexpr QByteArrayView ACTIVATION_MESSAGE{"activate"};

	// One endpoint per operating system user: the key is derived from the home directory.
	QString BuildServerName() {
		const QByteArray userKey = QCryptographicHash::hash(QDir::homePath().toUtf8(), QCryptographicHash::Sha256).toHex().left(USER_KEY_HEX_LENGTH);
		return QStringLiteral("%1-%2").arg(APPLICATION_ID, QLatin1StringView(userKey));
	}

	bool ActivateRunningInstance(const QString& serverName, std::chrono::milliseconds timeout) {
		QLocalSocket socket;
		socket.connectToServer(serverName, QIODevice::WriteOnly);
		if (!socket.waitForConnected(static_cast<int>(timeout.count()))) {
			return false;
		}
		socket.write(ACTIVATION_MESSAGE.data(), ACTIVATION_MESSAGE.size());
		socket.waitForBytesWritten(static_cast<int>(timeout.count()));
		socket.disconnectFromServer();
		return true;
	}
} // namespace

SingleInstanceGuard::SingleInstanceGuard(QObject* parent) :
	QObject(parent),
	serverName(BuildServerName()) {
}

InstanceAcquisition SingleInstanceGuard::Acquire() {
	if (ActivateRunningInstance(serverName, FIRST_ACTIVATION_TIMEOUT)) {
		return InstanceAcquisition::ACTIVATED_RUNNING_INSTANCE;
	}

	// After a failed connection a leftover endpoint can only belong to a crashed process.
	QLocalServer::removeServer(serverName);
	if (!activationServer.listen(serverName)) {
		// Two processes raced through startup: the one that owns the endpoint stays.
		if (ActivateRunningInstance(serverName, RACING_STARTUP_ACTIVATION_TIMEOUT)) {
			return InstanceAcquisition::ACTIVATED_RUNNING_INSTANCE;
		}
		return InstanceAcquisition::UNAVAILABLE;
	}

	connect(&activationServer, &QLocalServer::newConnection, this, &SingleInstanceGuard::AcceptActivationRequests);
	return InstanceAcquisition::ACQUIRED;
}

void SingleInstanceGuard::AcceptActivationRequests() {
	while (QLocalSocket* socket = activationServer.nextPendingConnection()) {
		socket->disconnectFromServer();
		socket->deleteLater();
	}
	Q_EMIT ActivationRequested();
}

} // namespace SteamForge
