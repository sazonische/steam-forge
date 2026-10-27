#pragma once

// Opens a page of a loopback server the way a browser follows the redirect of a sign-in page.
inline QByteArray VisitLoopback(quint16 port, const QByteArray& target) {
	constexpr int WAIT_MILLISECONDS = 5000;
	constexpr int READ_SLICE_MILLISECONDS = 10;
	QTcpSocket socket;
	socket.connectToHost(QHostAddress::LocalHost, port);
	if (!socket.waitForConnected(WAIT_MILLISECONDS)) {
		return {};
	}
	socket.write("GET " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\nAccept: text/html\r\n\r\n");
	QByteArray answer;
	QElapsedTimer timer;
	timer.start();
	while (socket.state() != QAbstractSocket::UnconnectedState && timer.elapsed() < WAIT_MILLISECONDS) {
		QCoreApplication::processEvents();
		socket.waitForReadyRead(READ_SLICE_MILLISECONDS);
		answer += socket.readAll();
	}
	return answer + socket.readAll();
}
