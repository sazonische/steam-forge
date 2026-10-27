#include "app/account_library.h"
#include "app/google_connection.h"
#include "app/storage_encryption.h"
#include "cloud/cloud_record.h"

#include "support/fake_google_transport.h"
#include "support/fake_key_store.h"
#include "support/loopback_browser.h"

namespace {

constexpr QLatin1StringView TOKEN_PATH{"/token"};
constexpr QLatin1StringView FILES_PATH{"/drive/v3/files"};
constexpr QLatin1StringView UPLOAD_PATH{"/upload/drive/v3/files"};
constexpr QLatin1StringView VAULT_FILE_ID{"vault-file"};
constexpr QLatin1StringView TEST_EMAIL{"player@example.com"};
constexpr QLatin1StringView TEST_REFRESH_TOKEN{"synthetic-refresh-token"};

SteamForge::GoogleEndpoints TestEndpoints() {
	return SteamForge::GoogleEndpoints{
		.authorizationUrl = QUrl(QStringLiteral("https://accounts.test/auth")),
		.tokenUrl = QUrl(QStringLiteral("https://oauth.test/token")),
		.revocationUrl = QUrl(QStringLiteral("https://oauth.test/revoke")),
		.driveFilesUrl = QUrl(QStringLiteral("https://drive.test/drive/v3/files")),
		.driveUploadUrl = QUrl(QStringLiteral("https://drive.test/upload/drive/v3/files")),
	};
}

QString UrlSafeBase64(const QByteArray& bytes) {
	return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray SignInTokens(const QString& scope) {
	const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("email"), QString(TEST_EMAIL)}}).toJson(QJsonDocument::Compact);
	return QJsonDocument(QJsonObject{
							 {QStringLiteral("access_token"), QStringLiteral("access")},
							 {QStringLiteral("refresh_token"), QString(TEST_REFRESH_TOKEN)},
							 {QStringLiteral("expires_in"), 3599},
							 {QStringLiteral("scope"), scope},
							 {QStringLiteral("id_token"), UrlSafeBase64("{}") + u'.' + UrlSafeBase64(payload) + QStringLiteral(".signature")},
						 })
		.toJson(QJsonDocument::Compact);
}

QByteArray FullAccessTokens() {
	return SignInTokens(QStringLiteral("openid https://www.googleapis.com/auth/userinfo.email https://www.googleapis.com/auth/drive.appdata"));
}

QJsonObject VaultFileObject() {
	return QJsonObject{
		{QStringLiteral("id"), QString(VAULT_FILE_ID)},
		{QStringLiteral("name"), QStringLiteral("vault.json")},
		{QStringLiteral("appProperties"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("vault")}}},
		{QStringLiteral("createdTime"), QStringLiteral("2026-10-27T10:00:00.000Z")},
	};
}

QJsonObject DriveFileObject(const QString& fileId, const QString& name, const QString& kind) {
	return QJsonObject{
		{QStringLiteral("id"), fileId},
		{QStringLiteral("name"), name},
		{QStringLiteral("appProperties"), QJsonObject{{QStringLiteral("kind"), kind}}},
		{QStringLiteral("createdTime"), QStringLiteral("2026-10-27T11:00:00.000Z")},
	};
}

QByteArray FileList(const QJsonArray& files) {
	return QJsonDocument(QJsonObject{{QStringLiteral("files"), files}}).toJson(QJsonDocument::Compact);
}

QByteArray UploadedVaultHeader(const QByteArray& uploadBody) {
	const qsizetype start = uploadBody.indexOf("{\"createdAt\"");
	const qsizetype end = uploadBody.indexOf("\r\n--", start);
	return start < 0 || end < 0 ? QByteArray() : uploadBody.sliced(start, end - start);
}

struct ConnectionFixture {
	QTemporaryDir directory;
	FakeKeyStore keyStore;
	std::unique_ptr<SteamForge::AccountLibrary> library;
	std::unique_ptr<SteamForge::StorageEncryption> encryption;
	FakeGoogleTransport transport;
	std::unique_ptr<SteamForge::GoogleConnection> connection;

	explicit ConnectionFixture(bool isEncrypted) {
		library = std::make_unique<SteamForge::AccountLibrary>(SteamForge::AuthenticatorStore(directory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(directory.filePath(QStringLiteral("sessions"))));
		encryption = std::make_unique<SteamForge::StorageEncryption>(keyStore, *library, directory.filePath(QStringLiteral("encryption.json")));
		encryption->PrepareLibrary();
		library->Load();
		if (isEncrypted && !encryption->Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value()) {
			qFatal("Encryption could not be turned on for the test.");
		}
		connection = NewConnection();
	}

	[[nodiscard]] std::unique_ptr<SteamForge::GoogleConnection> NewConnection() {
		const SteamForge::GoogleClient client{.clientId = QStringLiteral("client-id"), .clientSecret = {}};
		return std::make_unique<SteamForge::GoogleConnection>(transport, *encryption, client, TestEndpoints(), StateFilePath());
	}

	[[nodiscard]] QString StateFilePath() const {
		return directory.filePath(QStringLiteral("google.json"));
	}
};

// Starts the sign-in and answers the sign-in page like a browser after the user agreed.
QByteArray SignIn(SteamForge::GoogleConnection& connection, const QByteArray& answer = "code=synthetic-code") {
	QSignalSpy pageSpy(&connection, &SteamForge::GoogleConnection::SignInPageReady);
	connection.Connect();
	if (pageSpy.count() != 1) {
		return {};
	}
	const QUrlQuery query(pageSpy.front().front().toUrl());
	const QUrl redirectUri(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
	const QByteArray state = QUrl::toPercentEncoding(query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded));
	return VisitLoopback(static_cast<quint16>(redirectUri.port()), "/?state=" + state + '&' + answer);
}

SteamForge::GoogleConnectFailure FailureOf(const QSignalSpy& failedSpy) {
	return failedSpy.front().front().value<SteamForge::GoogleConnectFailure>();
}

} // namespace

class GoogleConnectionTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void CreatesTheVaultAfterTheKeyIsSaved() {
		ConnectionFixture fixture(true);
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, FullAccessTokens());
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({}));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, UPLOAD_PATH, QJsonDocument(VaultFileObject()).toJson(QJsonDocument::Compact));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({VaultFileObject()}));
		const QSignalSpy newVaultSpy(fixture.connection.get(), &SteamForge::GoogleConnection::NewVaultCreated);
		const QSignalSpy connectedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::Connected);

		QVERIFY(SignIn(*fixture.connection).startsWith("HTTP/1.1 200"));
		QTRY_COMPARE(newVaultSpy.count(), 1);
		QCOMPARE(fixture.connection->State(), SteamForge::GoogleConnectionState::CONNECTING);
		// Nothing reaches Drive before the user confirms the key is written down.
		QVERIFY(std::ranges::none_of(fixture.transport.SentRequests(), [](const SteamForge::GoogleRequest& request) {
			return request.url.path() == UPLOAD_PATH;
		}));
		fixture.connection->ConfirmRecoveryKeySaved();
		QTRY_COMPARE(connectedSpy.count(), 1);

		QCOMPARE(fixture.connection->State(), SteamForge::GoogleConnectionState::CONNECTED);
		QCOMPARE(fixture.connection->Email(), QString(TEST_EMAIL));
		QVERIFY(fixture.connection->Drive() != nullptr);
		const auto upload = std::ranges::find_if(fixture.transport.SentRequests(), [](const SteamForge::GoogleRequest& request) {
			return request.url.path() == UPLOAD_PATH;
		});
		QVERIFY(upload != fixture.transport.SentRequests().end());
		const std::expected<SteamForge::CloudVaultHeader, SteamForge::VaultHeaderError> uploadedHeader = SteamForge::ParseVaultHeader(UploadedVaultHeader(upload->body));
		QVERIFY(uploadedHeader.has_value());
		const std::optional<SteamForge::RecoveryKey> recoveryKey = SteamForge::RecoveryKey::Parse(newVaultSpy.front().front().toString());
		QVERIFY(recoveryKey.has_value());
		const std::optional<SteamForge::DataKey> openedKey = SteamForge::OpenWithRecoveryKey(*uploadedHeader, *recoveryKey);
		QVERIFY(openedKey.has_value() && fixture.connection->CloudKey().has_value());
		QCOMPARE(openedKey->Bytes(), fixture.connection->CloudKey()->Bytes());

		QFile stateFile(fixture.StateFilePath());
		QVERIFY(stateFile.open(QIODevice::ReadOnly));
		QVERIFY(!stateFile.readAll().contains(QString(TEST_REFRESH_TOKEN).toLatin1()));
		const std::unique_ptr<SteamForge::GoogleConnection> restarted = fixture.NewConnection();
		restarted->Load();
		QCOMPARE(restarted->State(), SteamForge::GoogleConnectionState::CONNECTED);
		QCOMPARE(restarted->Email(), QString(TEST_EMAIL));
		QCOMPARE(restarted->VaultId(), uploadedHeader->vaultId);
		QCOMPARE(restarted->CloudKey()->Bytes(), openedKey->Bytes());
	}

	void OpensAnExistingVaultOnlyWithItsKey() {
		ConnectionFixture fixture(true);
		const std::optional<SteamForge::DataKey> cloudKey = SteamForge::DataKey::Generate();
		const std::optional<SteamForge::RecoveryKey> recoveryKey = SteamForge::RecoveryKey::Generate();
		const std::optional<SteamForge::RecoveryKey> otherKey = SteamForge::RecoveryKey::Generate();
		QVERIFY(cloudKey.has_value() && recoveryKey.has_value() && otherKey.has_value());
		const QString vaultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		const std::optional<SteamForge::CloudVaultHeader> header = SteamForge::CreateVaultHeader(vaultId, QDateTime::currentDateTimeUtc(), *cloudKey, *recoveryKey);
		QVERIFY(header.has_value());
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, FullAccessTokens());
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({VaultFileObject()}));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, QString(FILES_PATH) + u'/' + VAULT_FILE_ID, SteamForge::SerializeVaultHeader(*header));
		const QSignalSpy keyNeededSpy(fixture.connection.get(), &SteamForge::GoogleConnection::RecoveryKeyNeeded);
		const QSignalSpy rejectedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::RecoveryKeyRejected);
		const QSignalSpy connectedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::Connected);

		QVERIFY(SignIn(*fixture.connection).startsWith("HTTP/1.1 200"));
		QTRY_COMPARE(keyNeededSpy.count(), 1);
		fixture.connection->OpenVault(*otherKey);
		QCOMPARE(rejectedSpy.count(), 1);
		QCOMPARE(fixture.connection->State(), SteamForge::GoogleConnectionState::CONNECTING);
		fixture.connection->OpenVault(*recoveryKey);
		QCOMPARE(connectedSpy.count(), 1);
		QCOMPARE(fixture.connection->VaultId(), vaultId);
		QCOMPARE(fixture.connection->CloudKey()->Bytes(), cloudKey->Bytes());
		QVERIFY(std::ranges::none_of(fixture.transport.SentRequests(), [](const SteamForge::GoogleRequest& request) {
			return request.url.path() == UPLOAD_PATH;
		}));
	}

	void SignsInAgainWithoutTheRecoveryKey() {
		ConnectionFixture fixture(true);
		const std::optional<SteamForge::DataKey> cloudKey = SteamForge::DataKey::Generate();
		const std::optional<SteamForge::RecoveryKey> recoveryKey = SteamForge::RecoveryKey::Generate();
		QVERIFY(cloudKey.has_value() && recoveryKey.has_value());
		const std::optional<SteamForge::CloudVaultHeader> header = SteamForge::CreateVaultHeader(QUuid::createUuid().toString(QUuid::WithoutBraces), QDateTime::currentDateTimeUtc(), *cloudKey, *recoveryKey);
		QVERIFY(header.has_value());
		const QByteArray headerJson = SteamForge::SerializeVaultHeader(*header);
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, FullAccessTokens());
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({VaultFileObject()}));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, QString(FILES_PATH) + u'/' + VAULT_FILE_ID, headerJson);
		QSignalSpy connectedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::Connected);
		QSignalSpy keyNeededSpy(fixture.connection.get(), &SteamForge::GoogleConnection::RecoveryKeyNeeded);
		QVERIFY(SignIn(*fixture.connection).startsWith("HTTP/1.1 200"));
		QTRY_COMPARE(keyNeededSpy.count(), 1);
		fixture.connection->OpenVault(*recoveryKey);
		QCOMPARE(connectedSpy.count(), 1);

		// Google refuses the access token and then the saved sign-in.
		fixture.transport.AddError(SteamForge::GoogleHttpMethod::GET, FILES_PATH, SteamForge::GoogleErrorKind::UNAUTHORIZED, 401);
		fixture.transport.AddError(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, SteamForge::GoogleErrorKind::SIGN_IN_REQUIRED, 400, QStringLiteral("invalid_grant"));
		fixture.connection->Drive()->ListFiles(*fixture.connection, [](const std::expected<std::vector<SteamForge::DriveFile>, SteamForge::GoogleError>&) {
		});
		QTRY_COMPARE(fixture.connection->State(), SteamForge::GoogleConnectionState::SIGN_IN_REQUIRED);

		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, FullAccessTokens());
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({VaultFileObject()}));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, QString(FILES_PATH) + u'/' + VAULT_FILE_ID, headerJson);
		keyNeededSpy.clear();
		connectedSpy.clear();
		QVERIFY(SignIn(*fixture.connection).startsWith("HTTP/1.1 200"));
		QTRY_COMPARE(connectedSpy.count(), 1);
		QCOMPARE(keyNeededSpy.count(), 0);
		QCOMPARE(fixture.connection->CloudKey()->Bytes(), cloudKey->Bytes());
	}

	void RequiresLocalEncryption() {
		ConnectionFixture fixture(false);
		const QSignalSpy failedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::ConnectFailed);
		const QSignalSpy pageSpy(fixture.connection.get(), &SteamForge::GoogleConnection::SignInPageReady);
		fixture.connection->Connect();
		QCOMPARE(failedSpy.count(), 1);
		QCOMPARE(FailureOf(failedSpy), SteamForge::GoogleConnectFailure::ENCRYPTION_OFF);
		QCOMPARE(pageSpy.count(), 0);
		QCOMPARE(fixture.connection->State(), SteamForge::GoogleConnectionState::DISCONNECTED);
	}

	void ReportsAnUntickedDrivePermission() {
		ConnectionFixture fixture(true);
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, SignInTokens(QStringLiteral("openid https://www.googleapis.com/auth/userinfo.email")));
		const QSignalSpy failedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::ConnectFailed);
		QVERIFY(SignIn(*fixture.connection).startsWith("HTTP/1.1 200"));
		QTRY_COMPARE(failedSpy.count(), 1);
		QCOMPARE(FailureOf(failedSpy), SteamForge::GoogleConnectFailure::DRIVE_PERMISSION_MISSING);
		QCOMPARE(fixture.connection->State(), SteamForge::GoogleConnectionState::DISCONNECTED);
		QVERIFY(!QFileInfo::exists(fixture.StateFilePath()));
	}

	void ReportsASignInCancelledOnTheGooglePage() {
		ConnectionFixture fixture(true);
		const QSignalSpy failedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::ConnectFailed);
		QVERIFY(SignIn(*fixture.connection, "error=access_denied").startsWith("HTTP/1.1 200"));
		QTRY_COMPARE(failedSpy.count(), 1);
		QCOMPARE(FailureOf(failedSpy), SteamForge::GoogleConnectFailure::SIGN_IN_CANCELLED);
		QVERIFY(fixture.transport.SentRequests().empty());
	}

	void ForgetsOnlyThisComputer() {
		ConnectionFixture fixture(true);
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, FullAccessTokens());
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({}));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, UPLOAD_PATH, QJsonDocument(VaultFileObject()).toJson(QJsonDocument::Compact));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({VaultFileObject()}));
		const QSignalSpy newVaultSpy(fixture.connection.get(), &SteamForge::GoogleConnection::NewVaultCreated);
		const QSignalSpy connectedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::Connected);
		QVERIFY(SignIn(*fixture.connection).startsWith("HTTP/1.1 200"));
		QTRY_COMPARE(newVaultSpy.count(), 1);
		fixture.connection->ConfirmRecoveryKeySaved();
		QTRY_COMPARE(connectedSpy.count(), 1);
		const std::size_t requestsBefore = fixture.transport.SentRequests().size();

		QVERIFY(fixture.connection->Disconnect().has_value());
		QCOMPARE(fixture.connection->State(), SteamForge::GoogleConnectionState::DISCONNECTED);
		QVERIFY(fixture.connection->Drive() == nullptr);
		QVERIFY(!fixture.connection->CloudKey().has_value());
		QVERIFY(!QFileInfo::exists(fixture.StateFilePath()));
		// Nothing is deleted on Google Drive and nothing is revoked.
		QCOMPARE(fixture.transport.SentRequests().size(), requestsBefore);
	}

	void CancellingDropsALateAnswer() {
		ConnectionFixture fixture(true);
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, FullAccessTokens());
		const QSignalSpy newVaultSpy(fixture.connection.get(), &SteamForge::GoogleConnection::NewVaultCreated);
		QSignalSpy pageSpy(fixture.connection.get(), &SteamForge::GoogleConnection::SignInPageReady);
		fixture.connection->Connect();
		QCOMPARE(pageSpy.count(), 1);
		const QUrl redirectUri(QUrlQuery(pageSpy.front().front().toUrl()).queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
		fixture.connection->CancelConnecting();
		QCOMPARE(fixture.connection->State(), SteamForge::GoogleConnectionState::DISCONNECTED);
		QTest::qWait(50);
		QVERIFY(!VisitLoopback(static_cast<quint16>(redirectUri.port()), "/?code=late").startsWith("HTTP/1.1 200"));
		QTest::qWait(50);
		QVERIFY(fixture.transport.SentRequests().empty());
		QCOMPARE(newVaultSpy.count(), 0);
	}

	void ReplacesALostVaultAfterTheNewKeyIsSaved() {
		ConnectionFixture fixture(true);
		const std::optional<SteamForge::DataKey> oldCloudKey = SteamForge::DataKey::Generate();
		const std::optional<SteamForge::RecoveryKey> lostKey = SteamForge::RecoveryKey::Generate();
		QVERIFY(oldCloudKey.has_value() && lostKey.has_value());
		const QString oldVaultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		const std::optional<SteamForge::CloudVaultHeader> oldHeader = SteamForge::CreateVaultHeader(oldVaultId, QDateTime::currentDateTimeUtc(), *oldCloudKey, *lostKey);
		QVERIFY(oldHeader.has_value());
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, FullAccessTokens());
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({VaultFileObject()}));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, QString(FILES_PATH) + u'/' + VAULT_FILE_ID, SteamForge::SerializeVaultHeader(*oldHeader));
		QSignalSpy keyNeededSpy(fixture.connection.get(), &SteamForge::GoogleConnection::RecoveryKeyNeeded);
		QVERIFY(SignIn(*fixture.connection).startsWith("HTTP/1.1 200"));
		QTRY_COMPARE(keyNeededSpy.count(), 1);

		const QSignalSpy newVaultSpy(fixture.connection.get(), &SteamForge::GoogleConnection::NewVaultCreated);
		const QSignalSpy connectedSpy(fixture.connection.get(), &SteamForge::GoogleConnection::Connected);
		fixture.connection->ReplaceVault();
		QCOMPARE(newVaultSpy.count(), 1);
		const auto isDeletion = [](const SteamForge::GoogleRequest& request) {
			return request.method == SteamForge::GoogleHttpMethod::DELETE_RESOURCE;
		};
		// Until the new key is saved, nothing of the old vault is touched.
		QVERIFY(std::ranges::none_of(fixture.transport.SentRequests(), isDeletion));

		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({VaultFileObject(), DriveFileObject(QStringLiteral("record-a"), QStringLiteral("record.json"), QStringLiteral("record")), DriveFileObject(QStringLiteral("record-b"), QStringLiteral("record.json"), QStringLiteral("record"))}));
		for (const QString& fileId : {QStringLiteral("record-a"), QStringLiteral("record-b"), QString(VAULT_FILE_ID)}) {
			fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::DELETE_RESOURCE, QString(FILES_PATH) + u'/' + fileId, QByteArray());
		}
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, UPLOAD_PATH, QJsonDocument(DriveFileObject(QStringLiteral("new-vault-file"), QStringLiteral("vault.json"), QStringLiteral("vault"))).toJson(QJsonDocument::Compact));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FileList({DriveFileObject(QStringLiteral("new-vault-file"), QStringLiteral("vault.json"), QStringLiteral("vault"))}));
		fixture.connection->ConfirmRecoveryKeySaved();
		QTRY_COMPARE(connectedSpy.count(), 1);

		QStringList deletedPaths;
		for (const SteamForge::GoogleRequest& request : fixture.transport.SentRequests()) {
			if (isDeletion(request)) {
				deletedPaths.append(request.url.path());
			}
		}
		QCOMPARE(deletedPaths.size(), 3);
		// The header goes last, so a reset cut short still asks for a key and can be done again.
		QCOMPARE(deletedPaths.back(), QString(FILES_PATH) + u'/' + VAULT_FILE_ID);
		QVERIFY(fixture.connection->VaultId() != oldVaultId);
		QVERIFY(fixture.connection->CloudKey()->Bytes() != oldCloudKey->Bytes());
		QVERIFY(fixture.connection->Links().empty());
		const std::optional<SteamForge::RecoveryKey> newKey = SteamForge::RecoveryKey::Parse(newVaultSpy.front().front().toString());
		QVERIFY(newKey.has_value());
		QVERIFY(newKey->Bytes() != lostKey->Bytes());
	}

	void IsUnavailableWithoutAClient() {
		ConnectionFixture fixture(true);
		const SteamForge::GoogleClient emptyClient;
		SteamForge::GoogleConnection connection(fixture.transport, *fixture.encryption, emptyClient, TestEndpoints(), fixture.StateFilePath());
		const QSignalSpy pageSpy(&connection, &SteamForge::GoogleConnection::SignInPageReady);
		QCOMPARE(connection.State(), SteamForge::GoogleConnectionState::UNAVAILABLE);
		connection.Connect();
		connection.Load();
		QCOMPARE(pageSpy.count(), 0);
		QCOMPARE(connection.State(), SteamForge::GoogleConnectionState::UNAVAILABLE);
	}
};

QTEST_GUILESS_MAIN(GoogleConnectionTest)

#include "google_connection_test.moc"
