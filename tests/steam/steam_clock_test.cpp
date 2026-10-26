#include "steam/steam_clock.h"

namespace {
using std::chrono::seconds;

// Shape of the public ITwoFactorService/QueryTime reply: 64-bit fields as strings, 32-bit as numbers.
constexpr QByteArrayView COMPLETE_REPLY{R"({"response":{"server_time":"1710000000","skew_tolerance_seconds":"60","large_time_jink":"86400","probe_frequency_seconds":3600,"adjusted_time_probe_frequency_seconds":300,"hint_probe_frequency_seconds":60,"sync_timeout":60,"try_again_seconds":900,"max_attempts":3}})"};

SteamForge::SteamTimeResponse ResponseAt(qint64 serverTime) {
	return {
		.serverTime = std::chrono::sys_seconds(seconds(serverTime)),
		.skewTolerance = std::nullopt,
		.probeInterval = std::nullopt,
		.adjustedTimeProbeInterval = std::nullopt,
		.minimumProbeInterval = std::nullopt,
		.retryInterval = std::nullopt,
	};
}
} // namespace

class SteamClockTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ParsesCompleteReply() {
		const std::optional<SteamForge::SteamTimeResponse> response = SteamForge::ParseQueryTimeResponse(COMPLETE_REPLY.toByteArray());
		QVERIFY(response.has_value());
		QCOMPARE(response->serverTime.time_since_epoch().count(), 1710000000);
		QCOMPARE(response->skewTolerance, std::optional(seconds(60)));
		QCOMPARE(response->probeInterval, std::optional(seconds(3600)));
		QCOMPARE(response->adjustedTimeProbeInterval, std::optional(seconds(300)));
		QCOMPARE(response->minimumProbeInterval, std::optional(seconds(60)));
		QCOMPARE(response->retryInterval, std::optional(seconds(900)));
	}

	void ParsesServerTimeAsNumber() {
		const std::optional<SteamForge::SteamTimeResponse> response = SteamForge::ParseQueryTimeResponse(R"({"response":{"server_time":1710000000}})");
		QVERIFY(response.has_value());
		QCOMPARE(response->serverTime.time_since_epoch().count(), 1710000000);
		QVERIFY(!response->probeInterval.has_value());
	}

	void RejectsMalformedReplies_data() {
		QTest::addColumn<QByteArray>("reply");
		QTest::newRow("empty") << QByteArray();
		QTest::newRow("not json") << QByteArray("<html>busy</html>");
		QTest::newRow("no response object") << QByteArray(R"({"server_time":"1710000000"})");
		QTest::newRow("no server time") << QByteArray(R"({"response":{}})");
		QTest::newRow("text server time") << QByteArray(R"({"response":{"server_time":"soon"}})");
		QTest::newRow("zero server time") << QByteArray(R"({"response":{"server_time":"0"}})");
		QTest::newRow("negative server time") << QByteArray(R"({"response":{"server_time":-5}})");
		QTest::newRow("implausible server time") << QByteArray(R"({"response":{"server_time":"99999999999999999"}})");
	}

	void RejectsMalformedReplies() {
		QFETCH(QByteArray, reply);
		QVERIFY(!SteamForge::ParseQueryTimeResponse(reply).has_value());
	}

	void AccurateClockUsesRegularProbeInterval() {
		SteamForge::SteamTimeResponse response = ResponseAt(1710000000);
		response.skewTolerance = seconds(60);
		response.probeInterval = seconds(3600);
		response.adjustedTimeProbeInterval = seconds(300);
		QCOMPARE(SteamForge::NextProbeDelay(response, std::chrono::seconds(2)), seconds(3600));
	}

	void SkewedClockUsesAdjustedProbeInterval() {
		SteamForge::SteamTimeResponse response = ResponseAt(1710000000);
		response.skewTolerance = seconds(60);
		response.probeInterval = seconds(3600);
		response.adjustedTimeProbeInterval = seconds(300);
		QCOMPARE(SteamForge::NextProbeDelay(response, std::chrono::seconds(-120)), seconds(300));
	}

	void HintRaisesTheProbeInterval() {
		SteamForge::SteamTimeResponse response = ResponseAt(1710000000);
		response.adjustedTimeProbeInterval = seconds(70);
		response.minimumProbeInterval = seconds(120);
		QCOMPARE(SteamForge::NextProbeDelay(response, std::chrono::seconds(30)), seconds(120));
	}

	void ProbeIntervalStaysWithinBounds() {
		SteamForge::SteamTimeResponse response = ResponseAt(1710000000);
		QCOMPARE(SteamForge::NextProbeDelay(response, std::chrono::seconds(0)), seconds(3600));
		response.adjustedTimeProbeInterval = seconds(1);
		QCOMPARE(SteamForge::NextProbeDelay(response, std::chrono::seconds(10)), seconds(60));
		response.adjustedTimeProbeInterval = std::chrono::hours(48);
		QCOMPARE(SteamForge::NextProbeDelay(response, std::chrono::seconds(10)), std::chrono::hours(24));
	}
};

QTEST_GUILESS_MAIN(SteamClockTest)

#include "steam_clock_test.moc"
