#pragma once

namespace SteamForge {

// Receives the answer of the Google sign-in page on 127.0.0.1, the way Google asks desktop programs to. Only an
// answer with the state of this sign-in counts, so another local program cannot slip in a code.
class GoogleRedirectReceiver final : public QObject {
	Q_OBJECT

public:
	explicit GoogleRedirectReceiver(QString expectedState, QObject* parent = nullptr);
	~GoogleRedirectReceiver() override;
	Q_DISABLE_COPY_MOVE(GoogleRedirectReceiver)

	// A free port of 127.0.0.1, chosen by the system.
	[[nodiscard]] bool Listen();
	[[nodiscard]] QString RedirectUri() const;

Q_SIGNALS:
	void CodeReceived(const QString& authorizationCode);
	// The user declined on the Google page, or Google reported another error.
	void SignInRefused(const QString& reason);

private:
	void Read(QTcpSocket& socket);

	QTcpServer server;
	QString state;
	bool isAnswered = false;
	QHash<const QTcpSocket*, QByteArray> receivedBySocket;
	std::vector<QPointer<QTcpSocket>> openSockets;
};

} // namespace SteamForge
