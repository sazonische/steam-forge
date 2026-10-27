#include "cloud/google_redirect_receiver.h"

namespace SteamForge {

namespace {
	constexpr qsizetype MAX_REQUEST_SIZE = qsizetype{16} << 10;
	constexpr QByteArrayView HEADER_END{"\r\n\r\n"};
	constexpr QByteArrayView LINE_END{"\r\n"};
	constexpr QByteArrayView GET_METHOD{"GET"};
	constexpr QByteArrayView OK_STATUS{"200 OK"};
	constexpr QByteArrayView BAD_REQUEST_STATUS{"400 Bad Request"};
	constexpr QByteArrayView NOT_FOUND_STATUS{"404 Not Found"};
	constexpr QLatin1StringView LOOPBACK_ORIGIN{"http://127.0.0.1"};
	constexpr QLatin1StringView REDIRECT_URI_PATTERN{"http://127.0.0.1:%1"};
	constexpr QLatin1StringView ROOT_PATH{"/"};
	constexpr QLatin1StringView STATE_PARAMETER{"state"};
	constexpr QLatin1StringView CODE_PARAMETER{"code"};
	constexpr QLatin1StringView ERROR_PARAMETER{"error"};
	constexpr QLatin1StringView MISSING_CODE_REASON{"missing_code"};
	constexpr QLatin1StringView PAGE_PATTERN{
		"<!doctype html><html><head><meta charset=\"utf-8\"><title>SteamForge</title></head>"
		"<body style=\"margin:0;height:100vh;display:flex;align-items:center;justify-content:center;"
		"background:#171d25;color:#dcdedf;font:16px/1.5 sans-serif\"><p style=\"max-width:420px;text-align:center\">%1</p>"
		"</body></html>"
	};
	constexpr QLatin1StringView RESPONSE_HEADER_PATTERN{"HTTP/1.1 %1\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %2\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n"};
	constexpr int REQUEST_LINE_PART_COUNT = 3;

	void Answer(QTcpSocket& socket, QByteArrayView status, const QString& message) {
		const QByteArray page = QString(PAGE_PATTERN).arg(message.toHtmlEscaped()).toUtf8();
		socket.write(QString(RESPONSE_HEADER_PATTERN).arg(QString::fromLatin1(status), QString::number(page.size())).toLatin1());
		socket.write(page);
		socket.disconnectFromHost();
	}
} // namespace

GoogleRedirectReceiver::GoogleRedirectReceiver(QString expectedState, QObject* parent) :
	QObject(parent),
	state(std::move(expectedState)) {
	connect(&server, &QTcpServer::newConnection, this, [this]() {
		while (QTcpSocket* socket = server.nextPendingConnection()) {
			// The answer page must reach the browser even when the sign-in ends and the receiver goes right after it.
			socket->setParent(nullptr);
			openSockets.emplace_back(socket);
			connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
				Read(*socket);
			});
			connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
				receivedBySocket.remove(socket);
			});
		}
	});
}

GoogleRedirectReceiver::~GoogleRedirectReceiver() {
	// Connections a browser opened ahead and never used; answered ones close themselves once the page is sent.
	for (const QPointer<QTcpSocket>& trackedSocket : openSockets) {
		QTcpSocket* socket = trackedSocket.data();
		if (socket != nullptr && socket->bytesToWrite() == 0) {
			socket->abort();
			socket->deleteLater();
		}
	}
}

bool GoogleRedirectReceiver::Listen() {
	return server.listen(QHostAddress::LocalHost);
}

QString GoogleRedirectReceiver::RedirectUri() const {
	return QString(REDIRECT_URI_PATTERN).arg(server.serverPort());
}

void GoogleRedirectReceiver::Read(QTcpSocket& socket) {
	QByteArray& received = receivedBySocket[&socket];
	received += socket.readAll();
	if (received.size() > MAX_REQUEST_SIZE) {
		receivedBySocket.remove(&socket);
		socket.abort();
		return;
	}
	if (!received.contains(HEADER_END)) {
		return;
	}
	const QList<QByteArray> requestLine = received.first(received.indexOf(LINE_END)).split(' ');
	receivedBySocket.remove(&socket);
	if (requestLine.size() != REQUEST_LINE_PART_COUNT || requestLine.at(0) != GET_METHOD) {
		Answer(socket, BAD_REQUEST_STATUS, {});
		return;
	}
	const QUrl target = QUrl::fromEncoded(LOOPBACK_ORIGIN.toString().toLatin1() + requestLine.at(1), QUrl::StrictMode);
	// Browsers also ask for a favicon.
	if (!target.isValid() || target.path() != ROOT_PATH) {
		Answer(socket, NOT_FOUND_STATUS, {});
		return;
	}
	const QUrlQuery query(target);
	if (isAnswered || query.queryItemValue(STATE_PARAMETER, QUrl::FullyDecoded) != state) {
		Answer(socket, BAD_REQUEST_STATUS, tr("This page does not belong to the sign-in SteamForge started."));
		return;
	}
	isAnswered = true;
	server.close();
	const QString error = query.queryItemValue(ERROR_PARAMETER, QUrl::FullyDecoded);
	if (!error.isEmpty()) {
		Answer(socket, OK_STATUS, tr("Signing in to Google was cancelled. You can close this tab and return to SteamForge."));
		Q_EMIT SignInRefused(error);
		return;
	}
	const QString authorizationCode = query.queryItemValue(CODE_PARAMETER, QUrl::FullyDecoded);
	if (authorizationCode.isEmpty()) {
		Answer(socket, OK_STATUS, tr("Google sent an answer SteamForge does not understand. Return to SteamForge and try again."));
		Q_EMIT SignInRefused(MISSING_CODE_REASON);
		return;
	}
	Answer(socket, OK_STATUS, tr("Done. You can close this tab and return to SteamForge."));
	Q_EMIT CodeReceived(authorizationCode);
}

} // namespace SteamForge
