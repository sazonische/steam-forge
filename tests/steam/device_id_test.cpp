#include "steam/device_id.h"

class DeviceIdTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	// Expected values follow node-steam-totp getDeviceID, recomputed independently in Python.
	void MatchesNodeSteamTotp_data() {
		QTest::addColumn<quint64>("steamId");
		QTest::addColumn<QString>("expectedDeviceId");
		QTest::newRow("first") << quint64{76561198000000001} << QStringLiteral("android:ca748b58-133d-73d0-adcd-109baa02c0fa");
		QTest::newRow("second") << quint64{76561198000000002} << QStringLiteral("android:557089d5-7887-0e7f-c3b9-e16e0a5276d6");
	}

	void MatchesNodeSteamTotp() {
		QFETCH(quint64, steamId);
		QFETCH(QString, expectedDeviceId);
		QCOMPARE(SteamForge::DeriveNodeSteamTotpDeviceId(steamId), expectedDeviceId);
	}

	void GeneratesDistinctAndroidStyleIds() {
		const QRegularExpression layout(QStringLiteral(R"(^android:[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$)"));
		const QString firstDeviceId = SteamForge::GenerateDeviceId();
		const QString secondDeviceId = SteamForge::GenerateDeviceId();
		QVERIFY(layout.match(firstDeviceId).hasMatch());
		QVERIFY(layout.match(secondDeviceId).hasMatch());
		QVERIFY(firstDeviceId != secondDeviceId);
	}
};

QTEST_GUILESS_MAIN(DeviceIdTest)

#include "device_id_test.moc"
