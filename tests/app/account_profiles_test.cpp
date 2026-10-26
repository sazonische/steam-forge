#include "app/account_profiles.h"
#include "steam/community_profile.h"
#include "steam/steam_image_cache.h"
#include "support/fake_community_transport.h"

namespace {
constexpr quint64 PROFILE_STEAM_ID = 76561198000000001;
constexpr QLatin1StringView PROFILE_PATH{"profiles/76561198000000001/"};
constexpr QLatin1StringView OLD_AVATAR_URL{"https://avatars.fastly.steamstatic.com/0123456789abcdef0123456789abcdef01234567_full.jpg"};
constexpr QLatin1StringView NEW_AVATAR_URL{"https://avatars.fastly.steamstatic.com/fedcba9876543210fedcba9876543210fedcba98_full.jpg"};
constexpr QLatin1StringView CACHE_FILE_PATH{"cache/profiles.json"};
constexpr int AVATAR_EDGE = 64;
constexpr qint64 TWO_DAYS_SECONDS = std::chrono::seconds(std::chrono::days(2)).count();
// Time for a request or signal that should not happen to show up.
constexpr int SETTLE_MILLISECONDS = 50;

QByteArray ProfileXml(const QString& personaName, QLatin1StringView avatarUrl) {
	return QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID64>76561198000000001</steamID64><steamID><![CDATA[%1]]></steamID><avatarFull><![CDATA[%2]]></avatarFull></profile>)").arg(personaName, avatarUrl).toUtf8();
}

QImage Picture(Qt::GlobalColor color) {
	QImage picture(AVATAR_EDGE, AVATAR_EDGE, QImage::Format_RGB32);
	picture.fill(color);
	return picture;
}

// The pictures are in memory before the test starts, so nothing is downloaded from Steam.
void AddPictures(SteamForge::SteamImageCache& images) {
	images.Insert(QUrl(OLD_AVATAR_URL), Picture(Qt::red));
	images.Insert(QUrl(NEW_AVATAR_URL), Picture(Qt::blue));
	images.Insert(SteamForge::DefaultAvatarUrl(), Picture(Qt::gray));
}

void CreateCache(const QString& cachePath) {
	SteamForge::SteamImageCache images;
	AddPictures(images);
	FakeCommunityTransport transport;
	transport.AddResponse(PROFILE_PATH, ProfileXml(QStringLiteral("Synthetic Name"), OLD_AVATAR_URL));
	SteamForge::AccountProfiles profiles(transport, images, cachePath);
	const QSignalSpy changedSpy(&profiles, &SteamForge::AccountProfiles::ProfilesChanged);
	QVERIFY(profiles.Profile(PROFILE_STEAM_ID).personaName.isEmpty());
	QTRY_VERIFY(!changedSpy.isEmpty());
}

QJsonObject ReadCache(const QString& cachePath) {
	QFile file(cachePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QJsonDocument::fromJson(file.readAll()).object();
}

// As if Steam was last asked two days ago.
void AgeCache(const QString& cachePath) {
	QJsonObject cache = ReadCache(cachePath);
	QJsonObject savedProfiles = cache.value(QStringLiteral("profiles")).toObject();
	QJsonObject saved = savedProfiles.value(QString::number(PROFILE_STEAM_ID)).toObject();
	saved.insert(QStringLiteral("checkedAt"), QDateTime::currentDateTimeUtc().addSecs(-TWO_DAYS_SECONDS).toString(Qt::ISODateWithMs));
	savedProfiles.insert(QString::number(PROFILE_STEAM_ID), saved);
	cache.insert(QStringLiteral("profiles"), savedProfiles);
	QFile file(cachePath);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	file.write(QJsonDocument(cache).toJson());
}
} // namespace

class AccountProfilesTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void SavesTheSteamNameAndAvatar() {
		const QTemporaryDir directory;
		const QString cachePath = directory.filePath(CACHE_FILE_PATH);
		SteamForge::SteamImageCache images;
		AddPictures(images);
		FakeCommunityTransport transport;
		transport.AddResponse(PROFILE_PATH, ProfileXml(QStringLiteral("Synthetic Name"), OLD_AVATAR_URL));
		SteamForge::AccountProfiles profiles(transport, images, cachePath);
		const QSignalSpy changedSpy(&profiles, &SteamForge::AccountProfiles::ProfilesChanged);

		QVERIFY(profiles.Profile(PROFILE_STEAM_ID).personaName.isEmpty());
		QTRY_VERIFY(!changedSpy.isEmpty());
		const SteamForge::AccountProfile profile = profiles.Profile(PROFILE_STEAM_ID);
		QCOMPARE(profile.personaName, QStringLiteral("Synthetic Name"));
		QCOMPARE(profile.avatar.pixelColor(0, 0), QColor(Qt::red));
		const std::vector<SteamForge::CommunityRequest> sentRequests = transport.SentTo(PROFILE_PATH);
		QCOMPARE(sentRequests.size(), std::size_t{1});
		const SteamForge::CommunityRequest& sentRequest = sentRequests.front();
		QVERIFY(sentRequest.session.accessToken.isEmpty());
		QCOMPARE(ParameterValue(sentRequest.parameters, u"xml"), QStringLiteral("1"));
		QVERIFY(QFileInfo::exists(cachePath));
	}

	void ShowsTheSavedProfileWithoutAskingSteam() {
		const QTemporaryDir directory;
		const QString cachePath = directory.filePath(CACHE_FILE_PATH);
		CreateCache(cachePath);
		// Nothing in memory: the avatar has to come from the saved file.
		SteamForge::SteamImageCache emptyImages;
		FakeCommunityTransport transport;
		SteamForge::AccountProfiles profiles(transport, emptyImages, cachePath);

		const SteamForge::AccountProfile profile = profiles.Profile(PROFILE_STEAM_ID);
		QCOMPARE(profile.personaName, QStringLiteral("Synthetic Name"));
		QCOMPARE(profile.avatar.pixelColor(0, 0), QColor(Qt::red));
		QTest::qWait(SETTLE_MILLISECONDS);
		QCOMPARE(transport.SentCount(), std::size_t{0});
	}

	void AsksSteamAgainAfterADay() {
		const QTemporaryDir directory;
		const QString cachePath = directory.filePath(CACHE_FILE_PATH);
		CreateCache(cachePath);
		AgeCache(cachePath);
		SteamForge::SteamImageCache images;
		AddPictures(images);
		FakeCommunityTransport transport;
		transport.AddResponse(PROFILE_PATH, ProfileXml(QStringLiteral("Renamed"), NEW_AVATAR_URL));
		SteamForge::AccountProfiles profiles(transport, images, cachePath);
		const QSignalSpy changedSpy(&profiles, &SteamForge::AccountProfiles::ProfilesChanged);

		QCOMPARE(profiles.Profile(PROFILE_STEAM_ID).personaName, QStringLiteral("Synthetic Name"));
		QTRY_VERIFY(!changedSpy.isEmpty());
		const SteamForge::AccountProfile profile = profiles.Profile(PROFILE_STEAM_ID);
		QCOMPARE(profile.personaName, QStringLiteral("Renamed"));
		QCOMPARE(profile.avatar.pixelColor(0, 0), QColor(Qt::blue));
		QCOMPARE(transport.SentTo(PROFILE_PATH).size(), std::size_t{1});
	}

	void KeepsTheSavedProfileWhenSteamIsUnreachable() {
		const QTemporaryDir directory;
		const QString cachePath = directory.filePath(CACHE_FILE_PATH);
		CreateCache(cachePath);
		AgeCache(cachePath);
		SteamForge::SteamImageCache emptyImages;
		FakeCommunityTransport transport;
		transport.AddFailure(PROFILE_PATH, SteamForge::WebApiErrorKind::NETWORK);
		SteamForge::AccountProfiles profiles(transport, emptyImages, cachePath);
		const QSignalSpy changedSpy(&profiles, &SteamForge::AccountProfiles::ProfilesChanged);

		QCOMPARE(profiles.Profile(PROFILE_STEAM_ID).personaName, QStringLiteral("Synthetic Name"));
		QTest::qWait(SETTLE_MILLISECONDS);
		const SteamForge::AccountProfile profile = profiles.Profile(PROFILE_STEAM_ID);
		QCOMPARE(profile.personaName, QStringLiteral("Synthetic Name"));
		QCOMPARE(profile.avatar.pixelColor(0, 0), QColor(Qt::red));
		// The next attempt waits; repainting the list does not ask Steam again.
		QCOMPARE(transport.SentTo(PROFILE_PATH).size(), std::size_t{1});
		QCOMPARE(changedSpy.count(), 0);
	}

	void SavesTheQuestionMarkAvatarForAccountsWithoutSteamId() {
		const QTemporaryDir directory;
		const QString cachePath = directory.filePath(CACHE_FILE_PATH);
		{
			SteamForge::SteamImageCache images;
			AddPictures(images);
			FakeCommunityTransport transport;
			SteamForge::AccountProfiles profiles(transport, images, cachePath);
			const SteamForge::AccountProfile profile = profiles.Profile(std::nullopt);
			QVERIFY(profile.personaName.isEmpty());
			QCOMPARE(profile.avatar.pixelColor(0, 0), QColor(Qt::gray));
			QCOMPARE(transport.SentCount(), std::size_t{0});
		}
		SteamForge::SteamImageCache emptyImages;
		FakeCommunityTransport transport;
		SteamForge::AccountProfiles restored(transport, emptyImages, cachePath);
		QCOMPARE(restored.Profile(std::nullopt).avatar.pixelColor(0, 0), QColor(Qt::gray));
	}

	void ForgetsAccountsThatLeft() {
		const QTemporaryDir directory;
		const QString cachePath = directory.filePath(CACHE_FILE_PATH);
		CreateCache(cachePath);
		SteamForge::SteamImageCache emptyImages;
		FakeCommunityTransport transport;
		SteamForge::AccountProfiles profiles(transport, emptyImages, cachePath);

		profiles.KeepOnly({});
		QVERIFY(ReadCache(cachePath).value(QStringLiteral("profiles")).toObject().isEmpty());
	}

	void IgnoresAnAnswerForAnAccountThatLeft() {
		const QTemporaryDir directory;
		const QString cachePath = directory.filePath(CACHE_FILE_PATH);
		SteamForge::SteamImageCache images;
		AddPictures(images);
		FakeCommunityTransport transport;
		transport.AddResponse(PROFILE_PATH, ProfileXml(QStringLiteral("Synthetic Name"), OLD_AVATAR_URL));
		SteamForge::AccountProfiles profiles(transport, images, cachePath);
		const QSignalSpy changedSpy(&profiles, &SteamForge::AccountProfiles::ProfilesChanged);

		QVERIFY(profiles.Profile(PROFILE_STEAM_ID).personaName.isEmpty());
		profiles.KeepOnly({});
		QTest::qWait(SETTLE_MILLISECONDS);
		QCOMPARE(changedSpy.count(), 0);
		QVERIFY(!QFileInfo::exists(cachePath));
	}
};

QTEST_MAIN(AccountProfilesTest)

#include "account_profiles_test.moc"
