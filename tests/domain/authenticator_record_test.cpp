#include "domain/authenticator_record.h"

namespace {
constexpr quint64 ACCOUNT_STEAM_ID = 76561198000000001;
constexpr QLatin1StringView DEVICE_ID{"android:00000000-0000-0000-0000-000000000001"};
// Base64 of the public synthetic sequences 1..20 and 20..1.
constexpr QLatin1StringView SHARED_SECRET{"AQIDBAUGBwgJCgsMDQ4PEBESExQ="};
constexpr QLatin1StringView IDENTITY_SECRET{"FBMSERAPDg0MCwoJCAcGBQQDAgE="};

SteamForge::AuthenticatorRecord CodesOnlyRecord() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.label = QStringLiteral("trader");
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source.origin = SteamForge::RecordOrigin::OTPAUTH_URI;
	record.source.importedAt = record.createdAt;
	record.secrets.sharedSecret = QByteArray::fromBase64(QByteArray(SHARED_SECRET.data(), SHARED_SECRET.size()));
	return record;
}

SteamForge::AuthenticatorRecord FullImport() {
	SteamForge::AuthenticatorRecord record = CodesOnlyRecord();
	record.id = QUuid::createUuid();
	record.label.clear();
	record.source.origin = SteamForge::RecordOrigin::MA_FILE;
	record.account.steamId = ACCOUNT_STEAM_ID;
	record.account.accountName = QStringLiteral("Trader_Login");
	record.secrets.identitySecret = QByteArray::fromBase64(QByteArray(IDENTITY_SECRET.data(), IDENTITY_SECRET.size()));
	record.secrets.deviceId = DEVICE_ID;
	record.secrets.revocationCode = QStringLiteral("R12345");
	record.additionalFields.insert(QStringLiteral("Session"), QJsonObject{{QStringLiteral("SteamID"), QString::number(ACCOUNT_STEAM_ID)}});
	return record;
}
} // namespace

class AuthenticatorRecordTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ImportOfTheSameAuthenticatorAddsOnlyMissingFields() {
		const SteamForge::AuthenticatorRecord savedRecord = CodesOnlyRecord();
		const SteamForge::AuthenticatorRecord importedRecord = FullImport();

		const std::optional<SteamForge::AuthenticatorRecord> completedRecord = SteamForge::CompleteRecord(savedRecord, importedRecord);
		QVERIFY(completedRecord.has_value());
		QCOMPARE(completedRecord->id, savedRecord.id);
		QCOMPARE(completedRecord->label, savedRecord.label);
		QCOMPARE(completedRecord->source.origin, SteamForge::RecordOrigin::OTPAUTH_URI);
		QCOMPARE(completedRecord->account.steamId, std::optional<quint64>(ACCOUNT_STEAM_ID));
		QCOMPARE(completedRecord->secrets.identitySecret, importedRecord.secrets.identitySecret);
		QCOMPARE(completedRecord->secrets.deviceId, QString(DEVICE_ID));
		QCOMPARE(completedRecord->secrets.revocationCode, QStringLiteral("R12345"));
		QVERIFY(completedRecord->additionalFields.contains(QStringLiteral("Session")));
		QVERIFY(SteamForge::DetermineCapabilities(*completedRecord).canSignConfirmations);
	}

	void NothingIsCompletedWhenTheImportAddsNothing() {
		const SteamForge::AuthenticatorRecord fullRecord = FullImport();
		QVERIFY(!SteamForge::CompleteRecord(fullRecord, FullImport()).has_value());
		QVERIFY(!SteamForge::CompleteRecord(fullRecord, CodesOnlyRecord()).has_value());
	}

	void AccountNamesDifferingOnlyInCaseAgree() {
		SteamForge::AuthenticatorRecord savedRecord = CodesOnlyRecord();
		savedRecord.account.accountName = QStringLiteral("trader_login");
		const std::optional<SteamForge::AuthenticatorRecord> completedRecord = SteamForge::CompleteRecord(savedRecord, FullImport());
		QVERIFY(completedRecord.has_value());
		QCOMPARE(completedRecord->account.accountName, QStringLiteral("trader_login"));
	}

	void DisagreeingDataIsNeverMixedIn() {
		SteamForge::AuthenticatorRecord otherIdentity = FullImport();
		otherIdentity.secrets.identitySecret = QByteArray(20, '\x07');
		SteamForge::AuthenticatorRecord savedWithOtherIdentity = CodesOnlyRecord();
		savedWithOtherIdentity.secrets.identitySecret = QByteArray(20, '\x09');
		QVERIFY(!SteamForge::CompleteRecord(savedWithOtherIdentity, otherIdentity).has_value());

		SteamForge::AuthenticatorRecord savedWithOtherAccount = CodesOnlyRecord();
		savedWithOtherAccount.account.steamId = ACCOUNT_STEAM_ID + 1;
		QVERIFY(!SteamForge::CompleteRecord(savedWithOtherAccount, FullImport()).has_value());

		SteamForge::AuthenticatorRecord otherAuthenticator = FullImport();
		otherAuthenticator.secrets.sharedSecret = QByteArray(20, '\x05');
		QVERIFY(!SteamForge::CompleteRecord(CodesOnlyRecord(), otherAuthenticator).has_value());
	}

	void AnUnfinishedSetupIsNotCompleted() {
		SteamForge::AuthenticatorRecord pendingRecord = CodesOnlyRecord();
		pendingRecord.pendingActivation = SteamForge::PendingActivation{.confirmType = 1, .phoneNumberHint = {}};
		QVERIFY(!SteamForge::CompleteRecord(pendingRecord, FullImport()).has_value());
	}

	void AcceptsDataTypedByHand() {
		const SteamForge::AuthenticatorRecord record = CodesOnlyRecord();
		const SteamForge::ManualRecordData typedData{
			.steamId = QStringLiteral(" 76561198000000001 "),
			.identitySecret = IDENTITY_SECRET,
			.deviceId = DEVICE_ID,
			.revocationCode = QStringLiteral(" r12345 "),
		};
		const auto updatedRecord = SteamForge::ApplyManualRecordData(record, typedData);
		QVERIFY(updatedRecord.has_value());
		QCOMPARE(updatedRecord->account.steamId, std::optional<quint64>(ACCOUNT_STEAM_ID));
		QCOMPARE(updatedRecord->secrets.identitySecret, FullImport().secrets.identitySecret);
		QCOMPARE(updatedRecord->secrets.deviceId, QString(DEVICE_ID));
		QCOMPARE(updatedRecord->secrets.sharedSecret, record.secrets.sharedSecret);
		QCOMPARE(updatedRecord->secrets.revocationCode, QStringLiteral("R12345"));
		QVERIFY(SteamForge::DetermineCapabilities(*updatedRecord).canSignConfirmations);
	}

	void EmptyFieldsKeepTheSavedValuesAndASavedSteamIdStays() {
		const SteamForge::AuthenticatorRecord record = FullImport();
		const SteamForge::ManualRecordData typedData{
			.steamId = QStringLiteral("76561198000000002"),
			.identitySecret = {},
			.deviceId = {},
			.revocationCode = QStringLiteral("R99999"),
		};
		const auto updatedRecord = SteamForge::ApplyManualRecordData(record, typedData);
		QVERIFY(updatedRecord.has_value());
		QCOMPARE(updatedRecord->account.steamId, record.account.steamId);
		QCOMPARE(updatedRecord->secrets.identitySecret, record.secrets.identitySecret);
		QCOMPARE(updatedRecord->secrets.deviceId, record.secrets.deviceId);
		QCOMPARE(updatedRecord->secrets.revocationCode, record.secrets.revocationCode);
	}

	void RejectsMalformedValues_data() {
		QTest::addColumn<QString>("steamId");
		QTest::addColumn<QString>("identitySecret");
		QTest::addColumn<QString>("deviceId");
		QTest::addColumn<QString>("revocationCode");
		QTest::addColumn<int>("expectedError");
		QTest::newRow("text instead of a SteamID") << QStringLiteral("trader") << QString() << QString() << QString() << static_cast<int>(SteamForge::ManualRecordDataError::INVALID_STEAM_ID);
		QTest::newRow("group SteamID") << QStringLiteral("103582791429521408") << QString() << QString() << QString() << static_cast<int>(SteamForge::ManualRecordDataError::INVALID_STEAM_ID);
		QTest::newRow("key not in Base64") << QString() << QStringLiteral("not a key!") << QString() << QString() << static_cast<int>(SteamForge::ManualRecordDataError::INVALID_IDENTITY_SECRET);
		QTest::newRow("code key instead of the confirmation key") << QString() << QString(SHARED_SECRET) << QString() << QString() << static_cast<int>(SteamForge::ManualRecordDataError::IDENTITY_SECRET_IS_CODE_KEY);
		QTest::newRow("key of another length") << QString() << QStringLiteral("AQIDBA==") << QString() << QString() << static_cast<int>(SteamForge::ManualRecordDataError::INVALID_IDENTITY_SECRET);
		QTest::newRow("device ID without prefix") << QString() << QString() << QStringLiteral("00000000-0000-0000-0000-000000000001") << QString() << static_cast<int>(SteamForge::ManualRecordDataError::INVALID_DEVICE_ID);
		QTest::newRow("device ID without UUID") << QString() << QString() << QStringLiteral("android:phone") << QString() << static_cast<int>(SteamForge::ManualRecordDataError::INVALID_DEVICE_ID);
		QTest::newRow("recovery code without R") << QString() << QString() << QString() << QStringLiteral("12345") << static_cast<int>(SteamForge::ManualRecordDataError::INVALID_REVOCATION_CODE);
		QTest::newRow("recovery code too short") << QString() << QString() << QString() << QStringLiteral("R1234") << static_cast<int>(SteamForge::ManualRecordDataError::INVALID_REVOCATION_CODE);
	}

	void RejectsMalformedValues() {
		QFETCH(QString, steamId);
		QFETCH(QString, identitySecret);
		QFETCH(QString, deviceId);
		QFETCH(QString, revocationCode);
		QFETCH(int, expectedError);
		const SteamForge::ManualRecordData typedData{
			.steamId = steamId,
			.identitySecret = identitySecret,
			.deviceId = deviceId,
			.revocationCode = revocationCode,
		};
		const auto updatedRecord = SteamForge::ApplyManualRecordData(CodesOnlyRecord(), typedData);
		QVERIFY(!updatedRecord.has_value());
		QCOMPARE(static_cast<int>(updatedRecord.error()), expectedError);
	}
};

QTEST_GUILESS_MAIN(AuthenticatorRecordTest)

#include "authenticator_record_test.moc"
