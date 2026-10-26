#include "app/update_check.h"

#include "support/local_http_server.h"

namespace {

constexpr QLatin1StringView RELEASE_PATH{"/repos/sazonische/steam-forge/releases/latest"};

QByteArray JsonResponse(int httpStatus, const QByteArray& body) {
	return QStringLiteral("HTTP/1.1 %1 Status\r\nContent-Type: application/json\r\nContent-Length: %2\r\nConnection: close\r\n\r\n").arg(httpStatus).arg(body.size()).toLatin1() + body;
}

QByteArray ReleaseJson(QLatin1StringView tagName) {
	return QStringLiteral(R"({"tag_name":"%1","draft":false,"prerelease":false,"html_url":"https://github.com/sazonische/steam-forge/releases/tag/%1"})").arg(tagName).toLatin1();
}

} // namespace

class UpdateCheckTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ReadsTheVersionOfAPublishedRelease_data() {
		QTest::addColumn<QByteArray>("releaseJson");
		QTest::addColumn<QString>("expectedVersion");
		QTest::newRow("release") << ReleaseJson(QLatin1StringView("v0.2.0")) << QStringLiteral("0.2.0");
		QTest::newRow("largest parts") << ReleaseJson(QLatin1StringView("v65535.0.12")) << QStringLiteral("65535.0.12");
		QTest::newRow("prerelease") << QByteArray(R"({"tag_name":"v0.2.0","draft":false,"prerelease":true})") << QString();
		QTest::newRow("draft") << QByteArray(R"({"tag_name":"v0.2.0","draft":true,"prerelease":false})") << QString();
		QTest::newRow("tag without v") << ReleaseJson(QLatin1StringView("0.2.0")) << QString();
		QTest::newRow("two parts") << ReleaseJson(QLatin1StringView("v0.2")) << QString();
		QTest::newRow("leading zero") << ReleaseJson(QLatin1StringView("v0.02.0")) << QString();
		QTest::newRow("suffix") << ReleaseJson(QLatin1StringView("v0.2.0-beta")) << QString();
		QTest::newRow("too long part") << ReleaseJson(QLatin1StringView("v123456.0.0")) << QString();
		QTest::newRow("no tag") << QByteArray(R"({"draft":false,"prerelease":false})") << QString();
		QTest::newRow("GitHub error") << QByteArray(R"({"message":"Not Found","status":"404"})") << QString();
		QTest::newRow("not JSON") << QByteArray("<html>rate limited</html>") << QString();
		QTest::newRow("array") << QByteArray(R"([{"tag_name":"v0.2.0"}])") << QString();
	}

	void ReadsTheVersionOfAPublishedRelease() {
		QFETCH(QByteArray, releaseJson);
		QFETCH(QString, expectedVersion);
		const std::optional<QVersionNumber> version = SteamForge::ParseLatestReleaseVersion(releaseJson);
		if (expectedVersion.isEmpty()) {
			QVERIFY(!version.has_value());
			return;
		}
		QVERIFY(version.has_value());
		QCOMPARE(version->toString(), expectedVersion);
	}

	void OpensTheReleasePageOfTheVersion() {
		QCOMPARE(SteamForge::ReleasePageUrl(QVersionNumber(0, 2, 0)), QUrl(QStringLiteral("https://github.com/sazonische/steam-forge/releases/tag/v0.2.0")));
	}

	void ReportsANewerRelease() {
		const LocalHttpServer server(JsonResponse(200, ReleaseJson(QLatin1StringView("v0.10.0"))));
		SteamForge::UpdateCheck updateCheck(QVersionNumber(0, 9, 3), QUrl(server.Origin() + RELEASE_PATH));
		QSignalSpy foundSpy(&updateCheck, &SteamForge::UpdateCheck::NewerReleaseFound);

		updateCheck.CheckNow();

		QVERIFY(foundSpy.wait());
		QCOMPARE(foundSpy.front().front().value<QVersionNumber>(), QVersionNumber(0, 10, 0));
		QVERIFY(!updateCheck.IsChecking());
		QCOMPARE(server.CapturedRequests().size(), std::size_t{1});
		const CapturedRequest& request = server.CapturedRequests().front();
		QCOMPARE(request.requestLine, QByteArray("GET /repos/sazonische/steam-forge/releases/latest HTTP/1.1"));
		QCOMPARE(request.headers.value("user-agent"), QByteArray("SteamForge/0.9.3"));
		QCOMPARE(request.headers.value("accept"), QByteArray("application/vnd.github+json"));
		QVERIFY(!request.headers.contains("cookie"));
	}

	void StaysQuietWithoutANewerRelease_data() {
		QTest::addColumn<QByteArray>("response");
		QTest::newRow("same version") << JsonResponse(200, ReleaseJson(QLatin1StringView("v0.9.3")));
		QTest::newRow("older version") << JsonResponse(200, ReleaseJson(QLatin1StringView("v0.9.2")));
		QTest::newRow("no release yet") << JsonResponse(404, QByteArray(R"({"message":"Not Found"})"));
		QTest::newRow("rate limit") << JsonResponse(403, QByteArray(R"({"message":"API rate limit exceeded"})"));
		QTest::newRow("broken answer") << JsonResponse(200, QByteArray("{"));
	}

	void StaysQuietWithoutANewerRelease() {
		QFETCH(QByteArray, response);
		const LocalHttpServer server(response);
		SteamForge::UpdateCheck updateCheck(QVersionNumber(0, 9, 3), QUrl(server.Origin() + RELEASE_PATH));
		const QSignalSpy foundSpy(&updateCheck, &SteamForge::UpdateCheck::NewerReleaseFound);

		updateCheck.CheckNow();
		QVERIFY(updateCheck.IsChecking());

		QTRY_VERIFY(!updateCheck.IsChecking());
		QCOMPARE(server.CapturedRequests().size(), std::size_t{1});
		QCOMPARE(foundSpy.count(), 0);
	}

	void AsksOnceAtATime() {
		const LocalHttpServer server(JsonResponse(200, ReleaseJson(QLatin1StringView("v1.0.0"))));
		SteamForge::UpdateCheck updateCheck(QVersionNumber(0, 9, 3), QUrl(server.Origin() + RELEASE_PATH));
		QSignalSpy foundSpy(&updateCheck, &SteamForge::UpdateCheck::NewerReleaseFound);

		updateCheck.CheckNow();
		updateCheck.CheckNow();

		QVERIFY(foundSpy.wait());
		QTRY_VERIFY(!updateCheck.IsChecking());
		QCOMPARE(server.CapturedRequests().size(), std::size_t{1});
		QCOMPARE(foundSpy.count(), 1);
	}

	void StoppingDropsTheAnswer() {
		const LocalHttpServer server(JsonResponse(200, ReleaseJson(QLatin1StringView("v1.0.0"))));
		SteamForge::UpdateCheck updateCheck(QVersionNumber(0, 9, 3), QUrl(server.Origin() + RELEASE_PATH));
		const QSignalSpy foundSpy(&updateCheck, &SteamForge::UpdateCheck::NewerReleaseFound);

		updateCheck.CheckNow();
		updateCheck.Stop();

		QVERIFY(!updateCheck.IsChecking());
		QTest::qWait(200);
		QCOMPARE(foundSpy.count(), 0);
	}
};

QTEST_GUILESS_MAIN(UpdateCheckTest)

#include "update_check_test.moc"
