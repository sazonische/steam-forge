#include "cloud/cloud_record.h"

namespace {

constexpr quint64 TEST_STEAM_ID = 76561198000000001;

SteamForge::AuthenticatorRecord TestRecord() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source = SteamForge::RecordSource{.origin = SteamForge::RecordOrigin::MA_FILE, .fileName = QStringLiteral("synthetic.maFile"), .importedAt = record.createdAt};
	record.account.steamId = TEST_STEAM_ID;
	record.account.accountName = QStringLiteral("synthetic_user");
	record.secrets.sharedSecret = QByteArray(20, 0x11);
	record.secrets.identitySecret = QByteArray(20, 0x22);
	record.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-000000000001");
	record.secrets.revocationCode = QStringLiteral("R12345");
	record.additionalFields.insert(QStringLiteral("phone_number_hint"), QStringLiteral("00"));
	record.additionalFields.insert(QStringLiteral("Session"), QJsonObject{{QStringLiteral("RefreshToken"), QStringLiteral("synthetic.refresh.token")}});
	return record;
}

SteamForge::CloudRecordPlace TestPlace() {
	return SteamForge::CloudRecordPlace{.vaultId = QStringLiteral("vault"), .recordId = QStringLiteral("record"), .revision = 1};
}

} // namespace

class CloudRecordTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void OpensWhatItSealed() {
		const std::optional<SteamForge::DataKey> cloudKey = SteamForge::DataKey::Generate();
		QVERIFY(cloudKey.has_value());
		const SteamForge::AuthenticatorRecord record = TestRecord();
		const std::optional<QByteArray> content = SteamForge::SealCloudRecord(*cloudKey, TestPlace(), record);
		QVERIFY(content.has_value());
		QVERIFY(!content->contains("synthetic_user"));
		QVERIFY(!content->contains("R12345"));
		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::CloudRecordError> opened = SteamForge::OpenCloudRecord(*cloudKey, TestPlace(), *content);
		QVERIFY(opened.has_value());
		QCOMPARE(opened->id, record.id);
		QCOMPARE(opened->account.steamId, record.account.steamId);
		QCOMPARE(opened->account.accountName, record.account.accountName);
		QCOMPARE(opened->secrets.sharedSecret, record.secrets.sharedSecret);
		QCOMPARE(opened->secrets.identitySecret, record.secrets.identitySecret);
		QCOMPARE(opened->secrets.revocationCode, record.secrets.revocationCode);
		QCOMPARE(opened->additionalFields.value(QStringLiteral("phone_number_hint")).toString(), QStringLiteral("00"));
	}

	void LeavesTheSteamSignInOnTheComputer() {
		const std::optional<SteamForge::DataKey> cloudKey = SteamForge::DataKey::Generate();
		QVERIFY(cloudKey.has_value());
		const std::optional<QByteArray> content = SteamForge::SealCloudRecord(*cloudKey, TestPlace(), TestRecord());
		QVERIFY(content.has_value());
		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::CloudRecordError> opened = SteamForge::OpenCloudRecord(*cloudKey, TestPlace(), *content);
		QVERIFY(opened.has_value());
		QVERIFY(!opened->additionalFields.contains(QStringLiteral("Session")));
	}

	void OpensOnlyInItsPlace_data() {
		QTest::addColumn<QString>("vaultId");
		QTest::addColumn<QString>("recordId");
		QTest::addColumn<int>("revision");
		QTest::newRow("another vault") << QStringLiteral("other vault") << QStringLiteral("record") << 1;
		QTest::newRow("another record") << QStringLiteral("vault") << QStringLiteral("other record") << 1;
		QTest::newRow("another revision") << QStringLiteral("vault") << QStringLiteral("record") << 2;
	}

	void OpensOnlyInItsPlace() {
		QFETCH(QString, vaultId);
		QFETCH(QString, recordId);
		QFETCH(int, revision);
		const std::optional<SteamForge::DataKey> cloudKey = SteamForge::DataKey::Generate();
		QVERIFY(cloudKey.has_value());
		const std::optional<QByteArray> content = SteamForge::SealCloudRecord(*cloudKey, TestPlace(), TestRecord());
		QVERIFY(content.has_value());
		const SteamForge::CloudRecordPlace otherPlace{.vaultId = vaultId, .recordId = recordId, .revision = revision};
		QCOMPARE(SteamForge::OpenCloudRecord(*cloudKey, otherPlace, *content).error(), SteamForge::CloudRecordError::UNSEALING_FAILED);
	}

	void StaysClosedForAnotherKey() {
		const std::optional<SteamForge::DataKey> cloudKey = SteamForge::DataKey::Generate();
		const std::optional<SteamForge::DataKey> otherKey = SteamForge::DataKey::Generate();
		QVERIFY(cloudKey.has_value() && otherKey.has_value());
		const std::optional<QByteArray> content = SteamForge::SealCloudRecord(*cloudKey, TestPlace(), TestRecord());
		QVERIFY(content.has_value());
		QCOMPARE(SteamForge::OpenCloudRecord(*otherKey, TestPlace(), *content).error(), SteamForge::CloudRecordError::UNSEALING_FAILED);
		QCOMPARE(SteamForge::OpenCloudRecord(*cloudKey, TestPlace(), QByteArray("{\"format\":\"other\"}")).error(), SteamForge::CloudRecordError::NOT_A_RECORD);
	}
};

QTEST_GUILESS_MAIN(CloudRecordTest)

#include "cloud_record_test.moc"
