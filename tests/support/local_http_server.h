#pragma once

struct CapturedRequest {
	QByteArray requestLine;
	QHash<QByteArray, QByteArray> headers;
	QByteArray body;
};

// Answers every HTTP/1.1 connection with one fixed response and keeps the requests it received.
class LocalHttpServer final {
public:
	explicit LocalHttpServer(QByteArrayView cannedResponse) :
		response(cannedResponse.toByteArray()) {
		QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
			while (QTcpSocket* socket = server.nextPendingConnection()) {
				QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket]() {
					Read(*socket);
				});
				QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
			}
		});
		server.listen(QHostAddress::LocalHost);
	}

	Q_DISABLE_COPY_MOVE(LocalHttpServer)
	~LocalHttpServer() = default;

	[[nodiscard]] QString Origin() const {
		return QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort());
	}

	[[nodiscard]] const std::vector<CapturedRequest>& CapturedRequests() const {
		return capturedRequests;
	}

private:
	void Read(QTcpSocket& socket) {
		constexpr QByteArrayView HEADER_END{"\r\n\r\n"};
		constexpr QByteArrayView CONTENT_LENGTH_HEADER{"content-length"};
		QByteArray& received = receivedBySocket[&socket];
		received += socket.readAll();
		const qsizetype headerEnd = received.indexOf(HEADER_END);
		if (headerEnd < 0) {
			return;
		}
		CapturedRequest request;
		const QList<QByteArray> headerLines = received.first(headerEnd).split('\n');
		request.requestLine = headerLines.front().trimmed();
		for (const QByteArray& headerLine : headerLines.sliced(1)) {
			const qsizetype separator = headerLine.indexOf(':');
			if (separator > 0) {
				request.headers.insert(headerLine.first(separator).trimmed().toLower(), headerLine.sliced(separator + 1).trimmed());
			}
		}
		const qsizetype bodyStart = headerEnd + HEADER_END.size();
		const qsizetype contentLength = request.headers.value(CONTENT_LENGTH_HEADER.toByteArray()).toLongLong();
		if (received.size() < bodyStart + contentLength) {
			return;
		}
		request.body = received.sliced(bodyStart, contentLength);
		receivedBySocket.remove(&socket);
		capturedRequests.push_back(std::move(request));
		socket.write(response);
		socket.disconnectFromHost();
	}

	QTcpServer server;
	QByteArray response;
	QHash<const QTcpSocket*, QByteArray> receivedBySocket;
	std::vector<CapturedRequest> capturedRequests;
};
