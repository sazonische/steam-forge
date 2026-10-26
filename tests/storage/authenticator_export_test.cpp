#include "storage/authenticator_export.h"
#include "storage/authenticator_import.h"

namespace {
constexpr quint64 ACCOUNT_STEAM_ID = 76561198000000001;
// Base64 of the public synthetic sequences 1..20 and 20..1.
constexpr QLatin1StringView SHARED_SECRET{"AQIDBAUGBwgJCgsMDQ4PEBESExQ="};
constexpr QLatin1StringView IDENTITY_SECRET{"FBMSERAPDg0MCwoJCAcGBQQDAgE="};
constexpr QLatin1StringView SHARED_SECRET_BASE32{"AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU"};
constexpr QLatin1StringView DEVICE_ID{"android:00000000-0000-0000-0000-000000000001"};

SteamForge::AuthenticatorRecord FullRecord() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.account.steamId = ACCOUNT_STEAM_ID;
	record.account.accountName = QStringLiteral("synthetic_user");
	record.secrets.sharedSecret = QByteArray::fromBase64(QByteArray(SHARED_SECRET.data(), SHARED_SECRET.size()));
	record.secrets.identitySecret = QByteArray::fromBase64(QByteArray(IDENTITY_SECRET.data(), IDENTITY_SECRET.size()));
	record.secrets.deviceId = DEVICE_ID;
	record.secrets.revocationCode = QStringLiteral("R12345");
	record.secrets.serialNumber = QStringLiteral("1234567890123456789");
	record.secrets.tokenGid = QStringLiteral("0123456789abcdef");
	record.additionalFields.insert(QStringLiteral("phone_number_hint"), QStringLiteral("00"));
	// Records imported before tokens were dropped may still keep them.
	record.additionalFields.insert(QStringLiteral("Session"), QJsonObject{{QStringLiteral("SteamID"), static_cast<qint64>(ACCOUNT_STEAM_ID)}, {QStringLiteral("RefreshToken"), QStringLiteral("synthetic.refresh.token")}});
	return record;
}
} // namespace

class AuthenticatorExportTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void WritesTheFieldsSteamDesktopAuthenticatorReads() {
		const QJsonObject exported = QJsonDocument::fromJson(SteamForge::ExportMaFile(FullRecord())).object();
		QCOMPARE(exported.value(QStringLiteral("shared_secret")).toString(), QString(SHARED_SECRET));
		QCOMPARE(exported.value(QStringLiteral("identity_secret")).toString(), QString(IDENTITY_SECRET));
		QCOMPARE(exported.value(QStringLiteral("device_id")).toString(), QString(DEVICE_ID));
		QCOMPARE(exported.value(QStringLiteral("revocation_code")).toString(), QStringLiteral("R12345"));
		QCOMPARE(exported.value(QStringLiteral("serial_number")).toString(), QStringLiteral("1234567890123456789"));
		QCOMPARE(exported.value(QStringLiteral("token_gid")).toString(), QStringLiteral("0123456789abcdef"));
		QCOMPARE(exported.value(QStringLiteral("account_name")).toString(), QStringLiteral("synthetic_user"));
		QCOMPARE(exported.value(QStringLiteral("uri")).toString(), QStringLiteral("otpauth://totp/Steam:synthetic_user?secret=%1&issuer=Steam").arg(SHARED_SECRET_BASE32));
		QCOMPARE(SteamForge::SteamGuardUri(FullRecord()), exported.value(QStringLiteral("uri")).toString());
		QVERIFY(exported.value(QStringLiteral("fully_enrolled")).toBool());
		QVERIFY(exported.contains(QStringLiteral("server_time")));
		QVERIFY(exported.contains(QStringLiteral("secret_1")));
		QVERIFY(exported.contains(QStringLiteral("status")));
		QCOMPARE(exported.value(QStringLiteral("phone_number_hint")).toString(), QStringLiteral("00"));
	}

	void KeepsSignInTokensOutOfTheFile() {
		const QByteArray exported = SteamForge::ExportMaFile(FullRecord());
		QVERIFY(!exported.contains("synthetic.refresh.token"));
		const QJsonObject session = QJsonDocument::fromJson(exported).object().value(QStringLiteral("Session")).toObject();
		QCOMPARE(session.keys(), QStringList{QStringLiteral("SteamID")});
		QVERIFY(exported.contains("\"SteamID\": 76561198000000001"));
	}

	void ImportsBackWithoutLosingData() {
		const SteamForge::AuthenticatorRecord original = FullRecord();
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> imported = SteamForge::ParseMaFile(SteamForge::ExportMaFile(original), SteamForge::MaFileName(original));
		QVERIFY(imported.has_value());
		QCOMPARE(imported->records.size(), std::size_t{1});
		const SteamForge::AuthenticatorRecord& record = imported->records.front();
		QCOMPARE(record.secrets.sharedSecret, original.secrets.sharedSecret);
		QCOMPARE(record.secrets.identitySecret, original.secrets.identitySecret);
		QCOMPARE(record.secrets.deviceId, original.secrets.deviceId);
		QCOMPARE(record.secrets.revocationCode, original.secrets.revocationCode);
		QCOMPARE(record.account.steamId, original.account.steamId);
		QCOMPARE(record.account.accountName, original.account.accountName);
	}

	void NamesTheFileLikeSteamDesktopAuthenticator() {
		QCOMPARE(SteamForge::MaFileName(FullRecord()), QStringLiteral("76561198000000001.maFile"));
		SteamForge::AuthenticatorRecord withoutSteamId = FullRecord();
		withoutSteamId.account.steamId.reset();
		withoutSteamId.account.accountName.clear();
		withoutSteamId.label = QStringLiteral("Steam:trader/1");
		QCOMPARE(SteamForge::MaFileName(withoutSteamId), QStringLiteral("Steam_trader_1.maFile"));
	}
};

QTEST_GUILESS_MAIN(AuthenticatorExportTest)

#include "authenticator_export_test.moc"
