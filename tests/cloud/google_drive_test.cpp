#include "cloud/google_drive.h"

#include "support/fake_google_transport.h"

namespace {

constexpr QLatin1StringView TOKEN_PATH{"/token"};
constexpr QLatin1StringView FILES_PATH{"/drive/v3/files"};
constexpr QLatin1StringView UPLOAD_PATH{"/upload/drive/v3/files"};

SteamForge::GoogleEndpoints TestEndpoints() {
	return SteamForge::GoogleEndpoints{
		.authorizationUrl = QUrl(QStringLiteral("https://accounts.test/auth")),
		.tokenUrl = QUrl(QStringLiteral("https://oauth.test/token")),
		.revocationUrl = QUrl(QStringLiteral("https://oauth.test/revoke")),
		.driveFilesUrl = QUrl(QStringLiteral("https://drive.test/drive/v3/files")),
		.driveUploadUrl = QUrl(QStringLiteral("https://drive.test/upload/drive/v3/files")),
	};
}

QByteArray RefreshedToken(const QString& accessToken) {
	return QJsonDocument(QJsonObject{{QStringLiteral("access_token"), accessToken}, {QStringLiteral("expires_in"), 3599}}).toJson(QJsonDocument::Compact);
}

QByteArray FilePage(const QStringList& fileIds, const QString& nextPageToken = {}) {
	QJsonArray files;
	for (const QString& fileId : fileIds) {
		files.append(QJsonObject{
			{QStringLiteral("id"), fileId},
			{QStringLiteral("name"), fileId + QStringLiteral(".json")},
			{QStringLiteral("appProperties"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("record")}}},
			{QStringLiteral("createdTime"), QStringLiteral("2026-10-27T10:00:00.000Z")},
		});
	}
	QJsonObject page{{QStringLiteral("files"), files}};
	if (!nextPageToken.isEmpty()) {
		page.insert(QStringLiteral("nextPageToken"), nextPageToken);
	}
	return QJsonDocument(page).toJson(QJsonDocument::Compact);
}

struct DriveFixture {
	FakeGoogleTransport transport;
	SteamForge::GoogleAccess access{transport, TestEndpoints(), SteamForge::GoogleClient{.clientId = QStringLiteral("client"), .clientSecret = {}}, QStringLiteral("refresh-token")};
	SteamForge::GoogleDrive drive{transport, access, TestEndpoints()};
	QObject context;
};

} // namespace

class GoogleDriveTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ListsEveryPageWithAFreshToken() {
		DriveFixture fixture;
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, RefreshedToken(QStringLiteral("access-1")));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FilePage({QStringLiteral("a"), QStringLiteral("b")}, QStringLiteral("page-2")));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FilePage({QStringLiteral("c")}));
		std::optional<std::expected<std::vector<SteamForge::DriveFile>, SteamForge::GoogleError>> listed;
		fixture.drive.ListFiles(fixture.context, [&listed](std::expected<std::vector<SteamForge::DriveFile>, SteamForge::GoogleError> files) {
			listed = std::move(files);
		});
		QTRY_VERIFY(listed.has_value());
		QVERIFY(listed->has_value());
		QCOMPARE((*listed)->size(), std::size_t{3});
		QCOMPARE((*listed)->at(2).id, QStringLiteral("c"));
		QCOMPARE((*listed)->at(0).appProperties.value(QStringLiteral("kind")), QStringLiteral("record"));
		const std::vector<SteamForge::GoogleRequest>& sent = fixture.transport.SentRequests();
		QCOMPARE(sent.size(), std::size_t{3});
		QCOMPARE(sent.at(1).accessToken, QStringLiteral("access-1"));
		const QUrlQuery firstQuery(sent.at(1).url);
		QCOMPARE(firstQuery.queryItemValue(QStringLiteral("spaces")), QStringLiteral("appDataFolder"));
		QVERIFY(!firstQuery.hasQueryItem(QStringLiteral("pageToken")));
		QCOMPARE(QUrlQuery(sent.at(2).url).queryItemValue(QStringLiteral("pageToken")), QStringLiteral("page-2"));
		QCOMPARE(sent.at(2).accessToken, QStringLiteral("access-1"));
	}

	void GetsANewTokenOnceWhenGoogleRefusesIt() {
		DriveFixture fixture;
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, RefreshedToken(QStringLiteral("access-1")));
		fixture.transport.AddError(SteamForge::GoogleHttpMethod::GET, FILES_PATH, SteamForge::GoogleErrorKind::UNAUTHORIZED, 401);
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, RefreshedToken(QStringLiteral("access-2")));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH, FilePage({}));
		std::optional<std::expected<std::vector<SteamForge::DriveFile>, SteamForge::GoogleError>> listed;
		fixture.drive.ListFiles(fixture.context, [&listed](std::expected<std::vector<SteamForge::DriveFile>, SteamForge::GoogleError> files) {
			listed = std::move(files);
		});
		QTRY_VERIFY(listed.has_value());
		QVERIFY(listed->has_value());
		QVERIFY((*listed)->empty());
		QCOMPARE(fixture.transport.SentRequests().back().accessToken, QStringLiteral("access-2"));
	}

	void AsksToSignInAgainWhenTheAccessIsRevoked() {
		DriveFixture fixture;
		fixture.transport.AddError(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, SteamForge::GoogleErrorKind::SIGN_IN_REQUIRED, 400, QStringLiteral("invalid_grant"));
		const QSignalSpy signInSpy(&fixture.access, &SteamForge::GoogleAccess::SignInRequired);
		std::optional<std::expected<QByteArray, SteamForge::GoogleError>> downloaded;
		fixture.drive.Download(QStringLiteral("file"), fixture.context, [&downloaded](std::expected<QByteArray, SteamForge::GoogleError> content) {
			downloaded = std::move(content);
		});
		QTRY_VERIFY(downloaded.has_value());
		QVERIFY(!downloaded->has_value());
		QCOMPARE(downloaded->error().kind, SteamForge::GoogleErrorKind::SIGN_IN_REQUIRED);
		QCOMPARE(signInSpy.count(), 1);
		QCOMPARE(fixture.transport.SentRequests().size(), std::size_t{1});
	}

	void UploadsMetadataAndContentTogether() {
		DriveFixture fixture;
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, RefreshedToken(QStringLiteral("access")));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, UPLOAD_PATH, QJsonDocument(QJsonObject{{QStringLiteral("id"), QStringLiteral("new-file")}, {QStringLiteral("name"), QStringLiteral("vault.json")}}).toJson(QJsonDocument::Compact));
		std::optional<std::expected<SteamForge::DriveFile, SteamForge::GoogleError>> created;
		const QByteArray content("sealed bytes");
		fixture.drive.Create(QStringLiteral("vault.json"), {{QStringLiteral("kind"), QStringLiteral("vault")}}, content, fixture.context, [&created](std::expected<SteamForge::DriveFile, SteamForge::GoogleError> file) {
			created = std::move(file);
		});
		QTRY_VERIFY(created.has_value());
		QVERIFY(created->has_value());
		QCOMPARE((*created)->id, QStringLiteral("new-file"));
		const SteamForge::GoogleRequest& upload = fixture.transport.SentRequests().back();
		QCOMPARE(QUrlQuery(upload.url).queryItemValue(QStringLiteral("uploadType")), QStringLiteral("multipart"));
		const QRegularExpressionMatch boundary = QRegularExpression(QStringLiteral("^multipart/related; boundary=(\\w+)$")).match(QString::fromLatin1(upload.contentType));
		QVERIFY(boundary.hasMatch());
		const QByteArray separator = "--" + boundary.captured(1).toLatin1();
		QVERIFY(upload.body.startsWith(separator + "\r\n"));
		QVERIFY(upload.body.endsWith("\r\n" + separator + "--"));
		QVERIFY(upload.body.contains("\"parents\":[\"appDataFolder\"]"));
		QVERIFY(upload.body.contains("\"appProperties\":{\"kind\":\"vault\"}"));
		QVERIFY(upload.body.contains("\r\n\r\n" + content + "\r\n"));
	}

	void DeletesAndDownloadsByFileId() {
		DriveFixture fixture;
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::POST, TOKEN_PATH, RefreshedToken(QStringLiteral("access")));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::GET, FILES_PATH + QStringLiteral("/file-1"), QByteArray("content"));
		fixture.transport.AddResponse(SteamForge::GoogleHttpMethod::DELETE_RESOURCE, FILES_PATH + QStringLiteral("/file-1"), QByteArray());
		std::optional<std::expected<QByteArray, SteamForge::GoogleError>> downloaded;
		std::optional<std::expected<void, SteamForge::GoogleError>> deleted;
		fixture.drive.Download(QStringLiteral("file-1"), fixture.context, [&downloaded](std::expected<QByteArray, SteamForge::GoogleError> content) {
			downloaded = std::move(content);
		});
		QTRY_VERIFY(downloaded.has_value());
		QCOMPARE(downloaded->value(), QByteArray("content"));
		QCOMPARE(QUrlQuery(fixture.transport.SentRequests().back().url).queryItemValue(QStringLiteral("alt")), QStringLiteral("media"));
		fixture.drive.Delete(QStringLiteral("file-1"), fixture.context, [&deleted](std::expected<void, SteamForge::GoogleError> outcome) {
			deleted = outcome;
		});
		QTRY_VERIFY(deleted.has_value());
		QVERIFY(deleted->has_value());
		QCOMPARE(fixture.transport.PendingResponseCount(), 0);
	}
};

QTEST_GUILESS_MAIN(GoogleDriveTest)

#include "google_drive_test.moc"
