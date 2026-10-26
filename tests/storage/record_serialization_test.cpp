#include "storage/record_serialization.h"

namespace {
constexpr quint64 SYNTHETIC_STEAM_ID = 76561198000000001;
constexpr int EMAIL_CONFIRM_TYPE = 3;

SteamForge::AuthenticatorRecord CompleteRecord() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::fromString(QStringLiteral("0b1c2d3e-4f50-4617-8293-a4b5c6d7e8f9"));
	record.label = QStringLiteral("Main account");
	record.createdAt = QDateTime(QDate(2026, 10, 25), QTime(10, 30, 15, 250), QTimeZone::UTC);
	record.account.steamId = SYNTHETIC_STEAM_ID;
	record.account.accountName = QStringLiteral("synthetic_user");
	record.secrets.sharedSecret = QByteArray::fromHex("0102030405060708090a0b0c0d0e0f1011121314");
	record.secrets.identitySecret = QByteArray::fromHex("14131211100f0e0d0c0b0a090807060504030201");
	record.secrets.revocationCode = QStringLiteral("R12345");
	record.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-000000000001");
	record.secrets.serialNumber = QStringLiteral("1234567890123456789");
	record.secrets.tokenGid = QStringLiteral("0123456789abcdef");
	record.source.origin = SteamForge::RecordOrigin::SDA_ENCRYPTED_MA_FILE;
	record.source.fileName = QStringLiteral("76561198000000001.maFile");
	record.source.importedAt = record.createdAt;
	record.additionalFields.insert(QStringLiteral("phone_number_hint"), QStringLiteral("00"));
	record.additionalFields.insert(QStringLiteral("server_time"), 1700000000);
	return record;
}
} // namespace

class RecordSerializationTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void RoundTripKeepsEveryField() {
		const SteamForge::AuthenticatorRecord original = CompleteRecord();
		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::StorageError> restored = SteamForge::DeserializeRecord(SteamForge::SerializeRecord(original));
		QVERIFY(restored.has_value());
		QCOMPARE(restored->id, original.id);
		QCOMPARE(restored->label, original.label);
		QCOMPARE(restored->createdAt, original.createdAt);
		QCOMPARE(restored->account.steamId, original.account.steamId);
		QCOMPARE(restored->account.accountName, original.account.accountName);
		QCOMPARE(restored->secrets.sharedSecret, original.secrets.sharedSecret);
		QCOMPARE(restored->secrets.identitySecret, original.secrets.identitySecret);
		QCOMPARE(restored->secrets.revocationCode, original.secrets.revocationCode);
		QCOMPARE(restored->secrets.deviceId, original.secrets.deviceId);
		QCOMPARE(restored->secrets.serialNumber, original.secrets.serialNumber);
		QCOMPARE(restored->secrets.tokenGid, original.secrets.tokenGid);
		QCOMPARE(restored->source.origin, original.source.origin);
		QCOMPARE(restored->source.fileName, original.source.fileName);
		QCOMPARE(restored->source.importedAt, original.source.importedAt);
		QCOMPARE(restored->additionalFields, original.additionalFields);
	}

	void SteamIdIsWrittenAsTextToKeepAll64Bits() {
		const QJsonObject document = SteamForge::SerializeRecord(CompleteRecord());
		QCOMPARE(document.value(u"account").toObject().value(u"steamId").toString(), QStringLiteral("76561198000000001"));
		QCOMPARE(document.value(u"formatVersion").toInteger(), qint64{SteamForge::RECORD_FORMAT_VERSION});
	}

	void RecordWithOnlyACodeKeyRoundTrips() {
		SteamForge::AuthenticatorRecord record;
		record.id = QUuid::createUuid();
		record.createdAt = QDateTime::currentDateTimeUtc();
		record.source.origin = SteamForge::RecordOrigin::OTPAUTH_URI;
		record.source.importedAt = record.createdAt;
		record.secrets.sharedSecret = QByteArray::fromHex("0102030405");
		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::StorageError> restored = SteamForge::DeserializeRecord(SteamForge::SerializeRecord(record));
		QVERIFY(restored.has_value());
		QVERIFY(!restored->account.steamId.has_value());
		QVERIFY(restored->secrets.identitySecret.isEmpty());
		QVERIFY(!SteamForge::DetermineCapabilities(*restored).canSignConfirmations);
	}

	void PendingActivationRoundTripsAndDisablesCodes() {
		SteamForge::AuthenticatorRecord record = CompleteRecord();
		record.source.origin = SteamForge::RecordOrigin::ADDED_IN_STEAMFORGE;
		record.pendingActivation = SteamForge::PendingActivation{.confirmType = EMAIL_CONFIRM_TYPE, .phoneNumberHint = QStringLiteral("12")};
		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::StorageError> restored = SteamForge::DeserializeRecord(SteamForge::SerializeRecord(record));
		QVERIFY(restored.has_value());
		QCOMPARE(restored->source.origin, SteamForge::RecordOrigin::ADDED_IN_STEAMFORGE);
		QVERIFY(restored->pendingActivation.has_value());
		QCOMPARE(restored->pendingActivation->confirmType, EMAIL_CONFIRM_TYPE);
		QCOMPARE(restored->pendingActivation->phoneNumberHint, QStringLiteral("12"));
		QVERIFY(!SteamForge::DetermineCapabilities(*restored).canGenerateCodes);
		QVERIFY(!SteamForge::DetermineCapabilities(*restored).canSignConfirmations);
	}

	void SignedInRecordWithoutSecretsRoundTrips() {
		SteamForge::AuthenticatorRecord record;
		record.id = QUuid::createUuid();
		record.createdAt = QDateTime::currentDateTimeUtc();
		record.account.steamId = SYNTHETIC_STEAM_ID;
		record.account.accountName = QStringLiteral("synthetic_user");
		record.source.origin = SteamForge::RecordOrigin::STEAM_SIGN_IN;
		record.source.importedAt = record.createdAt;
		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::StorageError> restored = SteamForge::DeserializeRecord(SteamForge::SerializeRecord(record));
		QVERIFY(restored.has_value());
		QCOMPARE(restored->source.origin, SteamForge::RecordOrigin::STEAM_SIGN_IN);
		QVERIFY(restored->secrets.sharedSecret.isEmpty());
		QVERIFY(!restored->pendingActivation.has_value());
	}

	void RejectsNewerFormatVersion() {
		QJsonObject document = SteamForge::SerializeRecord(CompleteRecord());
		document.insert(QStringLiteral("formatVersion"), SteamForge::RECORD_FORMAT_VERSION + 1);
		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::StorageError> restored = SteamForge::DeserializeRecord(document);
		QVERIFY(!restored.has_value());
		QCOMPARE(restored.error().kind, SteamForge::StorageErrorKind::NEWER_FORMAT_VERSION);
	}

	void RejectsForeignDocument() {
		const QJsonObject maFileLike{{QStringLiteral("shared_secret"), QStringLiteral("AQID")}};
		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::StorageError> restored = SteamForge::DeserializeRecord(maFileLike);
		QVERIFY(!restored.has_value());
		QCOMPARE(restored.error().kind, SteamForge::StorageErrorKind::UNKNOWN_FORMAT);
	}

	void RejectsDamagedFields_data() {
		QTest::addColumn<QString>("section");
		QTest::addColumn<QString>("field");
		QTest::addColumn<QJsonValue>("value");
		QTest::newRow("secret is not base64") << QStringLiteral("secrets") << QStringLiteral("sharedSecret") << QJsonValue(QStringLiteral("not base64!"));
		QTest::newRow("secret is a number") << QStringLiteral("secrets") << QStringLiteral("identitySecret") << QJsonValue(42);
		QTest::newRow("steamid of a group") << QStringLiteral("account") << QStringLiteral("steamId") << QJsonValue(QStringLiteral("103582791429521412"));
		QTest::newRow("unknown origin") << QStringLiteral("source") << QStringLiteral("origin") << QJsonValue(QStringLiteral("somewhere"));
	}

	void RejectsDamagedFields() {
		QFETCH(QString, section);
		QFETCH(QString, field);
		QFETCH(QJsonValue, value);
		QJsonObject document = SteamForge::SerializeRecord(CompleteRecord());
		QJsonObject sectionObject = document.value(section).toObject();
		sectionObject.insert(field, value);
		document.insert(section, sectionObject);

		const std::expected<SteamForge::AuthenticatorRecord, SteamForge::StorageError> restored = SteamForge::DeserializeRecord(document);
		QVERIFY(!restored.has_value());
		QCOMPARE(restored.error().kind, SteamForge::StorageErrorKind::INVALID_CONTENT);
		QCOMPARE(restored.error().detail, field);
	}

	void RejectsMissingIdentity() {
		QJsonObject document = SteamForge::SerializeRecord(CompleteRecord());
		document.remove(QStringLiteral("id"));
		QVERIFY(!SteamForge::DeserializeRecord(document).has_value());
	}
};

QTEST_GUILESS_MAIN(RecordSerializationTest)

#include "record_serialization_test.moc"
