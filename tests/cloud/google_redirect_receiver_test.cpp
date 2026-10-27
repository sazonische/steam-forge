#include "cloud/google_redirect_receiver.h"

#include "support/loopback_browser.h"

namespace {

constexpr QLatin1StringView TEST_STATE{"expected-state"};

quint16 PortOf(const SteamForge::GoogleRedirectReceiver& receiver) {
	return static_cast<quint16>(QUrl(receiver.RedirectUri()).port());
}

} // namespace

class GoogleRedirectReceiverTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ListensOnlyOnTheLoopbackAddress() {
		SteamForge::GoogleRedirectReceiver receiver(TEST_STATE);
		QVERIFY(receiver.Listen());
		const QUrl redirectUri(receiver.RedirectUri());
		QCOMPARE(redirectUri.scheme(), QStringLiteral("http"));
		QCOMPARE(redirectUri.host(), QStringLiteral("127.0.0.1"));
		QVERIFY(redirectUri.port() > 0);
	}

	void PassesOnTheCodeOfThisSignIn() {
		SteamForge::GoogleRedirectReceiver receiver(TEST_STATE);
		QVERIFY(receiver.Listen());
		const QSignalSpy codeSpy(&receiver, &SteamForge::GoogleRedirectReceiver::CodeReceived);
		const QByteArray answer = VisitLoopback(PortOf(receiver), "/?state=expected-state&code=4%2F0Abc-def&scope=email");
		QVERIFY2(answer.startsWith("HTTP/1.1 200 OK"), answer.constData());
		QCOMPARE(codeSpy.count(), 1);
		QCOMPARE(codeSpy.front().front().toString(), QStringLiteral("4/0Abc-def"));
	}

	void IgnoresAnAnswerWithAnotherState() {
		SteamForge::GoogleRedirectReceiver receiver(TEST_STATE);
		QVERIFY(receiver.Listen());
		const QSignalSpy codeSpy(&receiver, &SteamForge::GoogleRedirectReceiver::CodeReceived);
		const QByteArray answer = VisitLoopback(PortOf(receiver), "/?state=other&code=stolen");
		QVERIFY(answer.startsWith("HTTP/1.1 400"));
		QCOMPARE(codeSpy.count(), 0);
		// The real answer still counts afterwards.
		QVERIFY(VisitLoopback(PortOf(receiver), "/?state=expected-state&code=real").startsWith("HTTP/1.1 200"));
		QCOMPARE(codeSpy.count(), 1);
	}

	void ReportsACancelledSignIn() {
		SteamForge::GoogleRedirectReceiver receiver(TEST_STATE);
		QVERIFY(receiver.Listen());
		const QSignalSpy refusedSpy(&receiver, &SteamForge::GoogleRedirectReceiver::SignInRefused);
		const QSignalSpy codeSpy(&receiver, &SteamForge::GoogleRedirectReceiver::CodeReceived);
		QVERIFY(VisitLoopback(PortOf(receiver), "/?state=expected-state&error=access_denied").startsWith("HTTP/1.1 200"));
		QCOMPARE(refusedSpy.count(), 1);
		QCOMPARE(refusedSpy.front().front().toString(), QStringLiteral("access_denied"));
		QCOMPARE(codeSpy.count(), 0);
	}

	void AnswersOtherPagesWithNotFound() {
		SteamForge::GoogleRedirectReceiver receiver(TEST_STATE);
		QVERIFY(receiver.Listen());
		const QSignalSpy codeSpy(&receiver, &SteamForge::GoogleRedirectReceiver::CodeReceived);
		QVERIFY(VisitLoopback(PortOf(receiver), "/favicon.ico").startsWith("HTTP/1.1 404"));
		QCOMPARE(codeSpy.count(), 0);
	}

	void StopsListeningAfterTheAnswer() {
		SteamForge::GoogleRedirectReceiver receiver(TEST_STATE);
		QVERIFY(receiver.Listen());
		const quint16 port = PortOf(receiver);
		const QSignalSpy codeSpy(&receiver, &SteamForge::GoogleRedirectReceiver::CodeReceived);
		QVERIFY(VisitLoopback(port, "/?state=expected-state&code=first").startsWith("HTTP/1.1 200"));
		QVERIFY(!VisitLoopback(port, "/?state=expected-state&code=second").startsWith("HTTP/1.1 200"));
		QCOMPARE(codeSpy.count(), 1);
	}
};

QTEST_GUILESS_MAIN(GoogleRedirectReceiverTest)

#include "google_redirect_receiver_test.moc"
