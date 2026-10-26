#include "steam/community_profile.h"

namespace {
constexpr quint64 PROFILE_STEAM_ID = 76561198000000001;
constexpr quint64 OTHER_STEAM_ID = 76561198000000002;
constexpr qsizetype LONG_NAME_LENGTH = 100;
constexpr qsizetype MAX_NAME_LENGTH = 64;
constexpr char16_t RIGHT_TO_LEFT_OVERRIDE = 0x202E;
constexpr char16_t ZERO_WIDTH_SPACE = 0x200B;
constexpr QLatin1StringView PROFILE_AVATAR_URL{"https://avatars.fastly.steamstatic.com/0123456789abcdef0123456789abcdef01234567_full.jpg"};
// The shape of steamcommunity.com/profiles/<id>/?xml=1: the profile comes before its groups, which carry names and avatars of their own.
constexpr QByteArrayView PROFILE_XML{R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><profile><steamID64>76561198000000001</steamID64><steamID><![CDATA[Synthetic Name]]></steamID><avatarIcon><![CDATA[https://avatars.fastly.steamstatic.com/0123456789abcdef0123456789abcdef01234567.jpg]]></avatarIcon><avatarFull><![CDATA[https://avatars.fastly.steamstatic.com/0123456789abcdef0123456789abcdef01234567_full.jpg]]></avatarFull><groups><group isPrimary="1"><groupName><![CDATA[Group]]></groupName><avatarFull><![CDATA[https://avatars.fastly.steamstatic.com/fedcba9876543210fedcba9876543210fedcba98_full.jpg]]></avatarFull></group></groups></profile>)"};
constexpr QByteArrayView GROUP_FIRST_PROFILE_XML{R"(<?xml version="1.0" encoding="UTF-8"?><profile><groups><group><steamID><![CDATA[Group]]></steamID><avatarFull><![CDATA[https://avatars.fastly.steamstatic.com/fedcba9876543210fedcba9876543210fedcba98_full.jpg]]></avatarFull></group></groups><steamID64>76561198000000001</steamID64><steamID><![CDATA[Synthetic Name]]></steamID><avatarFull><![CDATA[https://avatars.fastly.steamstatic.com/0123456789abcdef0123456789abcdef01234567_full.jpg]]></avatarFull></profile>)"};
constexpr QByteArrayView UNKNOWN_PROFILE_XML{R"(<?xml version="1.0" encoding="UTF-8" standalone="yes"?><response><error><![CDATA[The specified profile could not be found.]]></error></response>)"};
constexpr QByteArrayView FOREIGN_AVATAR_XML{R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID64>76561198000000001</steamID64><steamID><![CDATA[Synthetic Name]]></steamID><avatarFull><![CDATA[https://example.com/avatar.jpg]]></avatarFull></profile>)"};
constexpr QByteArrayView PROFILE_WITHOUT_AVATAR_XML{R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID64>76561198000000001</steamID64><steamID><![CDATA[Synthetic Name]]></steamID></profile>)"};
constexpr QByteArrayView PROFILE_WITHOUT_STEAM_ID_XML{R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID><![CDATA[Synthetic Name]]></steamID></profile>)"};
constexpr QByteArrayView HTML_PAGE{R"(<!DOCTYPE html><html><body>Sorry! An error was encountered while processing your request.</body></html>)"};
constexpr QByteArrayView TRUNCATED_XML{R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID64>76561198000000001</steamID64><steamID><![CDATA[Synth)"};

// Built from the code point: such characters stay out of the source itself.
QString WithInvisibleCharacter(char16_t codePoint) {
	return QStringLiteral("Na") + QChar(codePoint) + QStringLiteral("me");
}

QByteArray ProfileWithName(const QString& personaName) {
	return QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID64>76561198000000001</steamID64><steamID><![CDATA[%1]]></steamID></profile>)").arg(personaName).toUtf8();
}
} // namespace

class CommunityProfileTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ReadsTheProfileItself_data() {
		QTest::addColumn<QByteArray>("profileXml");
		QTest::newRow("groups after the profile") << PROFILE_XML.toByteArray();
		QTest::newRow("groups before the profile") << GROUP_FIRST_PROFILE_XML.toByteArray();
	}

	void ReadsTheProfileItself() {
		QFETCH(QByteArray, profileXml);
		const std::optional<SteamForge::CommunityProfile> profile = SteamForge::ParseCommunityProfile(profileXml, PROFILE_STEAM_ID);
		QVERIFY(profile.has_value());
		QCOMPARE(profile->personaName, QStringLiteral("Synthetic Name"));
		QCOMPARE(profile->avatarUrl, QUrl(PROFILE_AVATAR_URL));
	}

	void FallsBackToTheQuestionMarkAvatar_data() {
		QTest::addColumn<QByteArray>("profileXml");
		QTest::addColumn<QString>("expectedName");
		QTest::newRow("unknown profile") << UNKNOWN_PROFILE_XML.toByteArray() << QString();
		QTest::newRow("picture outside the Steam servers") << FOREIGN_AVATAR_XML.toByteArray() << QStringLiteral("Synthetic Name");
		QTest::newRow("profile without an avatar") << PROFILE_WITHOUT_AVATAR_XML.toByteArray() << QStringLiteral("Synthetic Name");
	}

	void FallsBackToTheQuestionMarkAvatar() {
		QFETCH(QByteArray, profileXml);
		QFETCH(QString, expectedName);
		const std::optional<SteamForge::CommunityProfile> profile = SteamForge::ParseCommunityProfile(profileXml, PROFILE_STEAM_ID);
		QVERIFY(profile.has_value());
		QCOMPARE(profile->personaName, expectedName);
		QCOMPARE(profile->avatarUrl, SteamForge::DefaultAvatarUrl());
	}

	void TreatsOtherAnswersAsFailures_data() {
		QTest::addColumn<QByteArray>("answer");
		QTest::addColumn<quint64>("steamId");
		QTest::newRow("another account") << PROFILE_XML.toByteArray() << OTHER_STEAM_ID;
		QTest::newRow("no SteamID") << PROFILE_WITHOUT_STEAM_ID_XML.toByteArray() << PROFILE_STEAM_ID;
		QTest::newRow("error page") << HTML_PAGE.toByteArray() << PROFILE_STEAM_ID;
		QTest::newRow("cut off") << TRUNCATED_XML.toByteArray() << PROFILE_STEAM_ID;
		QTest::newRow("empty") << QByteArray() << PROFILE_STEAM_ID;
	}

	void TreatsOtherAnswersAsFailures() {
		QFETCH(QByteArray, answer);
		QFETCH(quint64, steamId);
		QVERIFY(!SteamForge::ParseCommunityProfile(answer, steamId).has_value());
	}

	void CleansTheName_data() {
		QTest::addColumn<QString>("personaName");
		QTest::addColumn<QString>("expectedName");
		QTest::newRow("spaces around") << QStringLiteral("  Name  ") << QStringLiteral("Name");
		QTest::newRow("right-to-left override") << WithInvisibleCharacter(RIGHT_TO_LEFT_OVERRIDE) << QStringLiteral("Name");
		QTest::newRow("zero-width space") << WithInvisibleCharacter(ZERO_WIDTH_SPACE) << QStringLiteral("Name");
		QTest::newRow("rich text stays text") << QStringLiteral("<b>Name</b>") << QStringLiteral("<b>Name</b>");
		QTest::newRow("too long") << QString(LONG_NAME_LENGTH, QLatin1Char('n')) << QString(MAX_NAME_LENGTH, QLatin1Char('n'));
	}

	void CleansTheName() {
		QFETCH(QString, personaName);
		QFETCH(QString, expectedName);
		const std::optional<SteamForge::CommunityProfile> profile = SteamForge::ParseCommunityProfile(ProfileWithName(personaName), PROFILE_STEAM_ID);
		QVERIFY(profile.has_value());
		QCOMPARE(profile->personaName, expectedName);
	}

	void BuildsThePublicProfilePath() {
		QCOMPARE(SteamForge::CommunityProfilePath(PROFILE_STEAM_ID), QStringLiteral("profiles/76561198000000001/"));
	}
};

QTEST_MAIN(CommunityProfileTest)

#include "community_profile_test.moc"
