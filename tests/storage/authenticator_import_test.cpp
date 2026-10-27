#include "storage/authenticator_import.h"
#include "storage/authenticator_store.h"

namespace {
// All fixtures are synthetic: secrets are byte sequences, the SteamID is above 2^53 to catch rounding.
constexpr quint64 SYNTHETIC_STEAM_ID = 76561198000000001;
constexpr QByteArrayView SHARED_SECRET_HEX{"0102030405060708090a0b0c0d0e0f1011121314"};
constexpr QByteArrayView IDENTITY_SECRET_HEX{"14131211100f0e0d0c0b0a090807060504030201"};

constexpr QByteArrayView COMPLETE_MA_FILE{R"({"shared_secret":"AQIDBAUGBwgJCgsMDQ4PEBESExQ=","serial_number":"1234567890123456789","revocation_code":"R12345","uri":"otpauth://totp/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&issuer=Steam","server_time":1700000000,"account_name":"synthetic_user","token_gid":"0123456789abcdef","identity_secret":"FBMSERAPDg0MCwoJCAcGBQQDAgE=","secret_1":"AAAAAAAAAAAAAAAAAAAAAAAAAAA=","status":1,"device_id":"android:00000000-0000-0000-0000-000000000001","fully_enrolled":true,"phone_number_hint":"00","Session":{"SteamID":76561198000000001,"AccessToken":null,"RefreshToken":null,"SessionID":null}})"};

// Encrypted by the scheme of Steam Desktop Authenticator: PBKDF2-HMAC-SHA1 (50 000) + AES-256-CBC, passkey below.
constexpr QLatin1StringView SDA_PASSKEY{"synthetic passkey"};
constexpr QByteArrayView SDA_MANIFEST{R"({"encrypted":true,"first_run":false,"entries":[{"encryption_iv":"EBESExQVFhcYGRobHB0eHw==","encryption_salt":"AQIDBAUGBwg=","filename":"76561198000000001.maFile","steamid":76561198000000001}],"periodic_checking":false})"};
constexpr QByteArrayView SDA_ENCRYPTED_MA_FILE{"6DMKu+DDV4vBRlt0n3LxKGBRk3zp2FfTDXKuzvlWKWkC26svN8C+TQoI7MPuU76h836+iLkCS0YE6UqAX/gecC9KyB4bB0p+1Xo19zbZ+H7390cJjjuzTJ9W1Qp5zc8deb7Ajv2CmNguQ/2O5X1iktsdUjbrclb8dRybZXkSRrb5Ux0v1i69O0GC89ygIDFBFYiruUB7EZTM8j9r4sQZyzi2cESBV1JseR70ZjVDi3xR9H0O2Xl9TnpVLkg8O/IsZzbaxRgUPHIOhtdUlNU6r/InGQ0LGDcLjODXPz9j2WpIJH4eLskSJeKITWo4YKitKIN5SJ5azbNYIwGQnQfewsUvHdisJYeM51DQOD4ku1lZxYjX2EI8oVfMmOVnlf5LjtN6PHuQrPOMTkKhWQSaqtV1i/26Dv2sprroTOFNePEHfbnqEKmqJtvBRX9mtfuuCMW5dxfhFX78wZEHsZZf6Q=="};
constexpr QLatin1StringView SDA_MA_FILE_NAME{"76561198000000001.maFile"};

// Same keys and value types as the file node-steamcommunity enable_twofactor.js writes.
constexpr QByteArrayView NODE_STEAMCOMMUNITY_FILE{R"({"shared_secret":"AQIDBAUGBwgJCgsMDQ4PEBESExQ=","serial_number":"1234567890123456789","revocation_code":"R12345","uri":"otpauth://totp/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&issuer=Steam","server_time":"1700000000","account_name":"synthetic_user","token_gid":"0123456789abcde","identity_secret":"FBMSERAPDg0MCwoJCAcGBQQDAgE=","secret_1":"AAAAAAAAAAAAAAAAAAAAAAAAAAA=","status":1,"phone_number_hint":"0000","confirm_type":1})"};
constexpr QLatin1StringView NODE_STEAMCOMMUNITY_FILE_NAME{"twofactor_76561198000000001.json"};
constexpr QLatin1StringView NODE_STEAM_TOTP_DEVICE_ID{"android:ca748b58-133d-73d0-adcd-109baa02c0fa"};

// WinAuth export line: Base32 secret, device ID and the Steam data JSON, form-encoded ('+' is a space).
constexpr QLatin1StringView WINAUTH_LINK{
	"otpauth://totp/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&digits=5&issuer=Steam"
	"&deviceid=android%3A00000000-0000-0000-0000-000000000001"
	"&data=%7B%22shared_secret%22%3A%22AQIDBAUGBwgJCgsMDQ4PEBESExQ%3D%22%2C%22identity_secret%22%3A%22FBMSERAPDg0MCwoJCAcGBQQDAgE%3D%22"
	"%2C%22revocation_code%22%3A%22R12345%22%2C%22account_name%22%3A%22synthetic+user%22%2C%22steamid%22%3A%2276561198000000001%22"
	"%2C%22serial_number%22%3A%221234567890123456789%22%2C%22status%22%3A1%7D"
};
constexpr QLatin1StringView WINAUTH_COMPAT_LINK{"otpauth://totp/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&digits=5&issuer=Steam"};
constexpr QLatin1StringView OTHER_SERVICE_LINK{"otpauth://totp/Example:someone?secret=MVTGO2DJNJVWY3LON5YHC4TTOR2XM53Y&issuer=Example"};
// The data JSON claims a different shared secret than the link itself.
constexpr QLatin1StringView MISMATCHED_LINK{"otpauth://totp/Steam:synthetic_user?secret=MVTGO2DJNJVWY3LON5YHC4TTOR2XM53Y&issuer=Steam&data=%7B%22shared_secret%22%3A%22AQIDBAUGBwgJCgsMDQ4PEBESExQ%3D%22%7D"};

QByteArray SharedSecret() {
	return QByteArray::fromHex(SHARED_SECRET_HEX.toByteArray());
}

QByteArray IdentitySecret() {
	return QByteArray::fromHex(IDENTITY_SECRET_HEX.toByteArray());
}

bool WriteFile(const QString& filePath, QByteArrayView contents) {
	QFile file(filePath);
	return file.open(QIODevice::WriteOnly) && file.write(contents.data(), contents.size()) == contents.size();
}

SteamForge::ImportErrorKind ErrorKindOf(const std::expected<SteamForge::ImportResult, SteamForge::ImportError>& result) {
	if (result.has_value()) {
		return SteamForge::ImportErrorKind::INVALID_CONTENT;
	}
	return result.error().kind;
}
} // namespace

class AuthenticatorImportTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void DropsTheSignInTokensOfTheSession() {
		QByteArray maFile = COMPLETE_MA_FILE.toByteArray();
		maFile.replace(R"("AccessToken":null,"RefreshToken":null,"SessionID":null)", R"("AccessToken":"synthetic.access.token","RefreshToken":"synthetic.refresh.token","SessionID":"0123456789abcdef01234567","SteamLoginSecure":"synthetic-cookie")");
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseMaFile(maFile, QStringLiteral("synthetic.maFile"));
		QVERIFY(result.has_value());
		const SteamForge::AuthenticatorRecord& record = result->records.front();
		QCOMPARE(record.account.steamId, std::optional(SYNTHETIC_STEAM_ID));
		const QByteArray stored = QJsonDocument(record.additionalFields).toJson(QJsonDocument::Compact);
		QVERIFY(!stored.contains("synthetic.access.token"));
		QVERIFY(!stored.contains("synthetic.refresh.token"));
		QVERIFY(!stored.contains("synthetic-cookie"));
		QVERIFY(!stored.contains("0123456789abcdef01234567"));
	}

	void CompleteMaFileKeepsAllData() {
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseMaFile(COMPLETE_MA_FILE.toByteArray(), QStringLiteral("synthetic.maFile"));
		QVERIFY(result.has_value());
		QCOMPARE(result->records.size(), std::size_t{1});
		const SteamForge::AuthenticatorRecord& record = result->records.front();
		QCOMPARE(record.secrets.sharedSecret, SharedSecret());
		QCOMPARE(record.secrets.identitySecret, IdentitySecret());
		QCOMPARE(record.secrets.revocationCode, QStringLiteral("R12345"));
		QCOMPARE(record.secrets.deviceId, QStringLiteral("android:00000000-0000-0000-0000-000000000001"));
		QCOMPARE(record.secrets.serialNumber, QStringLiteral("1234567890123456789"));
		QCOMPARE(record.secrets.tokenGid, QStringLiteral("0123456789abcdef"));
		QCOMPARE(record.account.accountName, QStringLiteral("synthetic_user"));
		QCOMPARE(record.account.steamId, std::optional(SYNTHETIC_STEAM_ID));
		QCOMPARE(record.source.origin, SteamForge::RecordOrigin::MA_FILE);
		QVERIFY(!record.id.isNull());

		for (const QString& preservedKey : {QStringLiteral("uri"), QStringLiteral("server_time"), QStringLiteral("secret_1"), QStringLiteral("status"), QStringLiteral("fully_enrolled"), QStringLiteral("phone_number_hint"), QStringLiteral("Session")}) {
			QVERIFY2(record.additionalFields.contains(preservedKey), qPrintable(preservedKey));
		}
		QVERIFY(!record.additionalFields.contains(QStringLiteral("shared_secret")));
		QCOMPARE(record.additionalFields.value(QStringLiteral("Session")).toObject().keys(), QStringList{QStringLiteral("SteamID")});

		const SteamForge::RecordCapabilities capabilities = SteamForge::DetermineCapabilities(record);
		QVERIFY(capabilities.canGenerateCodes);
		QVERIFY(capabilities.canSignConfirmations);
		QVERIFY(capabilities.hasRevocationCode);
	}

	void MaFileWithOnlyTheCodeKeyIsPartial() {
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseMaFile(R"({"shared_secret":"AQIDBAUGBwgJCgsMDQ4PEBESExQ=","account_name":"codes_only"})", QStringLiteral("partial.maFile"));
		QVERIFY(result.has_value());
		const SteamForge::RecordCapabilities capabilities = SteamForge::DetermineCapabilities(result->records.front());
		QVERIFY(capabilities.canGenerateCodes);
		QVERIFY(!capabilities.canSignConfirmations);
		QVERIFY(!capabilities.hasRevocationCode);
	}

	void RejectsDamagedMaFiles_data() {
		QTest::addColumn<QByteArray>("contents");
		QTest::addColumn<SteamForge::ImportErrorKind>("expectedError");
		// NOLINTNEXTLINE(modernize-raw-string-literal): moc counts an unbalanced brace inside a raw string and loses the slots that follow.
		QTest::newRow("truncated json") << QByteArray("{\"shared_secret\":\"AQID") << SteamForge::ImportErrorKind::INVALID_CONTENT;
		QTest::newRow("no shared secret") << QByteArray(R"({"account_name":"user"})") << SteamForge::ImportErrorKind::INVALID_CONTENT;
		QTest::newRow("shared secret not base64") << QByteArray(R"({"shared_secret":"%%%"})") << SteamForge::ImportErrorKind::INVALID_CONTENT;
		QTest::newRow("sda manifest") << SDA_MANIFEST.toByteArray() << SteamForge::ImportErrorKind::SDA_MANIFEST_SELECTED;
	}

	void RejectsDamagedMaFiles() {
		QFETCH(QByteArray, contents);
		QFETCH(SteamForge::ImportErrorKind, expectedError);
		QCOMPARE(ErrorKindOf(SteamForge::ParseMaFile(contents, QStringLiteral("damaged.maFile"))), expectedError);
	}

	// The fields of other programs are kept with the account, so they must fit in its file.
	void RefusesAFileTooLargeToKeep() {
		const QJsonObject maFile{
			{QStringLiteral("shared_secret"), QStringLiteral("AQIDBAUGBwgJCgsMDQ4PEBESExQ=")},
			{QStringLiteral("account_name"), QStringLiteral("synthetic_user")},
			{QStringLiteral("synthetic_notes"), QString(static_cast<qsizetype>(SteamForge::MAX_RECORD_DOCUMENT_SIZE), QLatin1Char('n'))},
		};
		QCOMPARE(ErrorKindOf(SteamForge::ParseMaFile(QJsonDocument(maFile).toJson(), QStringLiteral("large.maFile"))), SteamForge::ImportErrorKind::FILE_TOO_LARGE);
	}

	void ByteOrderMarkIsIgnored() {
		const QByteArray contents = QByteArray("\xEF\xBB\xBF") + COMPLETE_MA_FILE.toByteArray();
		QVERIFY(SteamForge::ParseMaFile(contents, QStringLiteral("bom.maFile")).has_value());
	}

	void WinAuthLinkWithDataIsComplete() {
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseOtpauthLinks(QString(WINAUTH_LINK), QStringLiteral("winauth.txt"));
		QVERIFY(result.has_value());
		QCOMPARE(result->records.size(), std::size_t{1});
		const SteamForge::AuthenticatorRecord& record = result->records.front();
		QCOMPARE(record.secrets.sharedSecret, SharedSecret());
		QCOMPARE(record.secrets.identitySecret, IdentitySecret());
		QCOMPARE(record.secrets.deviceId, QStringLiteral("android:00000000-0000-0000-0000-000000000001"));
		QCOMPARE(record.secrets.revocationCode, QStringLiteral("R12345"));
		QCOMPARE(record.account.accountName, QStringLiteral("synthetic user"));
		QCOMPARE(record.account.steamId, std::optional(SYNTHETIC_STEAM_ID));
		QCOMPARE(record.label, QStringLiteral("synthetic_user"));
		QCOMPARE(record.source.origin, SteamForge::RecordOrigin::OTPAUTH_URI);
		QVERIFY(SteamForge::DetermineCapabilities(record).canSignConfirmations);
	}

	void CompatLinkHasOnlyCodes() {
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseOtpauthLinks(QString(WINAUTH_COMPAT_LINK), QString());
		QVERIFY(result.has_value());
		const SteamForge::AuthenticatorRecord& record = result->records.front();
		QCOMPARE(record.secrets.sharedSecret, SharedSecret());
		QVERIFY(!record.account.steamId.has_value());
		QVERIFY(!SteamForge::DetermineCapabilities(record).canSignConfirmations);
	}

	void LinksOfOtherServicesAreSkipped() {
		const QString exportText = QString(OTHER_SERVICE_LINK) + QStringLiteral("\r\n\r\n") + QString(WINAUTH_COMPAT_LINK) + QStringLiteral("\r\n");
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseOtpauthLinks(exportText, QStringLiteral("winauth.txt"));
		QVERIFY(result.has_value());
		QCOMPARE(result->records.size(), std::size_t{1});
		QCOMPARE(result->skippedEntryCount, 1);
	}

	void OldWinAuthDataIsTheRecoveryCode() {
		const QString link = QString(WINAUTH_COMPAT_LINK) + QStringLiteral("&data=R54321");
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseOtpauthLinks(link, QString());
		QVERIFY(result.has_value());
		QCOMPARE(result->records.front().secrets.revocationCode, QStringLiteral("R54321"));
	}

	void SecretsOfDifferentAuthenticatorsAreNotCombined() {
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseOtpauthLinks(QString(MISMATCHED_LINK), QString());
		QCOMPARE(ErrorKindOf(result), SteamForge::ImportErrorKind::INCONSISTENT_SECRETS);
	}

	void DamagedLinkRejectsTheWholeExport() {
		const QString exportText = QString(WINAUTH_COMPAT_LINK) + QStringLiteral("\notpauth://totp/Steam:broken?secret=0189&issuer=Steam");
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ParseOtpauthLinks(exportText, QString());
		QVERIFY(!result.has_value());
		QCOMPARE(result.error().kind, SteamForge::ImportErrorKind::INVALID_CONTENT);
		QCOMPARE(result.error().detail, QStringLiteral("2"));
	}

	void EncryptedSdaFileOpensWithItsPasskey() {
		const QTemporaryDir temporaryDirectory;
		const QString maFilePath = temporaryDirectory.filePath(SDA_MA_FILE_NAME);
		QVERIFY(WriteFile(maFilePath, SDA_ENCRYPTED_MA_FILE));
		QVERIFY(WriteFile(temporaryDirectory.filePath(QStringLiteral("manifest.json")), SDA_MANIFEST));

		QCOMPARE(ErrorKindOf(SteamForge::ImportFile(maFilePath, std::nullopt)), SteamForge::ImportErrorKind::PASSKEY_REQUIRED);
		QCOMPARE(ErrorKindOf(SteamForge::ImportFile(maFilePath, QStringLiteral("wrong passkey"))), SteamForge::ImportErrorKind::PASSKEY_INCORRECT);

		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ImportFile(maFilePath, QString(SDA_PASSKEY));
		QVERIFY(result.has_value());
		const SteamForge::AuthenticatorRecord& record = result->records.front();
		QCOMPARE(record.source.origin, SteamForge::RecordOrigin::SDA_ENCRYPTED_MA_FILE);
		QCOMPARE(record.source.fileName, QString(SDA_MA_FILE_NAME));
		QCOMPARE(record.secrets.sharedSecret, SharedSecret());
		QCOMPARE(record.secrets.identitySecret, IdentitySecret());
		QCOMPARE(record.account.steamId, std::optional(SYNTHETIC_STEAM_ID));
	}

	void EncryptedSdaFileNeedsItsManifest() {
		const QTemporaryDir temporaryDirectory;
		const QString maFilePath = temporaryDirectory.filePath(SDA_MA_FILE_NAME);
		QVERIFY(WriteFile(maFilePath, SDA_ENCRYPTED_MA_FILE));
		QCOMPARE(ErrorKindOf(SteamForge::ImportFile(maFilePath, QString(SDA_PASSKEY))), SteamForge::ImportErrorKind::SDA_MANIFEST_MISSING);

		QVERIFY(WriteFile(temporaryDirectory.filePath(QStringLiteral("manifest.json")), R"({"encrypted":true,"entries":[]})"));
		QCOMPARE(ErrorKindOf(SteamForge::ImportFile(maFilePath, QString(SDA_PASSKEY))), SteamForge::ImportErrorKind::SDA_MANIFEST_ENTRY_MISSING);
	}

	void NodeSteamcommunityFileTakesSteamIdFromItsName() {
		const QTemporaryDir temporaryDirectory;
		const QString filePath = temporaryDirectory.filePath(NODE_STEAMCOMMUNITY_FILE_NAME);
		QVERIFY(WriteFile(filePath, NODE_STEAMCOMMUNITY_FILE));

		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ImportFile(filePath, std::nullopt);
		QVERIFY(result.has_value());
		const SteamForge::AuthenticatorRecord& record = result->records.front();
		QCOMPARE(record.source.origin, SteamForge::RecordOrigin::NODE_STEAMCOMMUNITY_FILE);
		QCOMPARE(record.account.steamId, std::optional(SYNTHETIC_STEAM_ID));
		QCOMPARE(record.account.accountName, QStringLiteral("synthetic_user"));
		QCOMPARE(record.secrets.deviceId, QString(NODE_STEAM_TOTP_DEVICE_ID));
		QCOMPARE(record.secrets.identitySecret, IdentitySecret());
		QVERIFY(SteamForge::DetermineCapabilities(record).canSignConfirmations);
		for (const QString& preservedKey : {QStringLiteral("server_time"), QStringLiteral("confirm_type"), QStringLiteral("phone_number_hint"), QStringLiteral("uri"), QStringLiteral("secret_1"), QStringLiteral("status")}) {
			QVERIFY2(record.additionalFields.contains(preservedKey), qPrintable(preservedKey));
		}
	}

	void RenamedNodeSteamcommunityFileStaysPartial() {
		const QTemporaryDir temporaryDirectory;
		const QString filePath = temporaryDirectory.filePath(QStringLiteral("renamed.json"));
		QVERIFY(WriteFile(filePath, NODE_STEAMCOMMUNITY_FILE));

		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> result = SteamForge::ImportFile(filePath, std::nullopt);
		QVERIFY(result.has_value());
		const SteamForge::AuthenticatorRecord& record = result->records.front();
		QCOMPARE(record.source.origin, SteamForge::RecordOrigin::MA_FILE);
		QVERIFY(!record.account.steamId.has_value());
		QVERIFY(record.secrets.deviceId.isEmpty());
		QVERIFY(!SteamForge::DetermineCapabilities(record).canSignConfirmations);
	}

	void ImportFileRecognisesFormatsByContent() {
		const QTemporaryDir temporaryDirectory;
		const QString maFilePath = temporaryDirectory.filePath(QStringLiteral("plain.maFile"));
		QVERIFY(WriteFile(maFilePath, COMPLETE_MA_FILE));
		QVERIFY(SteamForge::ImportFile(maFilePath, std::nullopt).has_value());

		const QString exportPath = temporaryDirectory.filePath(QStringLiteral("export.txt"));
		QVERIFY(WriteFile(exportPath, QByteArray(WINAUTH_LINK.data(), WINAUTH_LINK.size())));
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> exportResult = SteamForge::ImportFile(exportPath, std::nullopt);
		QVERIFY(exportResult.has_value());
		QCOMPARE(exportResult->records.front().source.origin, SteamForge::RecordOrigin::OTPAUTH_URI);

		const QString textPath = temporaryDirectory.filePath(QStringLiteral("notes.txt"));
		QVERIFY(WriteFile(textPath, "remember to buy milk"));
		QCOMPARE(ErrorKindOf(SteamForge::ImportFile(textPath, std::nullopt)), SteamForge::ImportErrorKind::UNRECOGNIZED_FORMAT);

		QCOMPARE(ErrorKindOf(SteamForge::ImportFile(temporaryDirectory.filePath(QStringLiteral("missing.maFile")), std::nullopt)), SteamForge::ImportErrorKind::FILE_UNREADABLE);
	}
};

QTEST_GUILESS_MAIN(AuthenticatorImportTest)

#include "authenticator_import_test.moc"
