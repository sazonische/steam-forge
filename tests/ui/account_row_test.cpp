#include "app/account_profiles.h"
#include "steam/steam_image_cache.h"
#include "support/fake_community_transport.h"
#include "ui/account_row.h"
#include "ui/elided_label.h"
#include "ui/record_presentation.h"

namespace {
constexpr quint64 ROW_STEAM_ID = 76561198000000001;
constexpr QLatin1StringView PROFILE_PATH{"profiles/76561198000000001/"};
constexpr QLatin1StringView AVATAR_URL{"https://avatars.fastly.steamstatic.com/0123456789abcdef0123456789abcdef01234567_full.jpg"};
constexpr QByteArrayView PROFILE_XML{R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID64>76561198000000001</steamID64><steamID><![CDATA[Synthetic Name]]></steamID><avatarFull><![CDATA[https://avatars.fastly.steamstatic.com/0123456789abcdef0123456789abcdef01234567_full.jpg]]></avatarFull></profile>)"};
constexpr QLatin1StringView ACCOUNT_NAME{"synthetic_login"};
constexpr QSize ROW_SIZE{1000, 64};
constexpr int AVATAR_EDGE = 64;

SteamForge::AuthenticatorRecord Record() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source.importedAt = record.createdAt;
	record.account.steamId = ROW_STEAM_ID;
	record.account.accountName = ACCOUNT_NAME;
	record.secrets.sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
	record.secrets.identitySecret = QByteArray::fromBase64("FBMSERAPDg0MCwoJCAcGBQQDAgE=");
	record.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-000000000000");
	return record;
}

// The title, then the line below it, as shown.
QStringList ShownNames(const SteamForge::AccountRow& row) {
	QStringList texts;
	for (const SteamForge::ElidedLabel* label : row.findChildren<SteamForge::ElidedLabel*>()) {
		texts.append(label->text());
	}
	return texts;
}
} // namespace

class AccountRowTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void HidesTheAccountNameBehindTheSteamName() {
		const QTemporaryDir directory;
		SteamForge::SteamImageCache images;
		QImage avatar(AVATAR_EDGE, AVATAR_EDGE, QImage::Format_RGB32);
		avatar.fill(Qt::red);
		images.Insert(QUrl(AVATAR_URL), avatar);
		FakeCommunityTransport transport;
		transport.AddResponse(PROFILE_PATH, PROFILE_XML.toByteArray());
		SteamForge::AccountProfiles profiles(transport, images, directory.filePath(QStringLiteral("profiles.json")));
		SteamForge::AccountRow row(Record(), profiles);
		// Checked before the event loop runs, since the answer of Steam may come while the row is shown. Until
		// then the account name is the only way to tell the row apart.
		QCOMPARE(row.accessibleName(), QString(ACCOUNT_NAME));
		row.resize(ROW_SIZE);
		row.show();
		QVERIFY(QTest::qWaitForWindowExposed(&row));
		const QSignalSpy revealSpy(&row, &SteamForge::AccountRow::RevealToggled);

		QTRY_COMPARE(ShownNames(row), (QStringList{QStringLiteral("Synthetic Name"), SteamForge::HiddenAccountName()}));

		QTest::keyClick(&row, Qt::Key_Space);
		QCOMPARE(ShownNames(row), (QStringList{QStringLiteral("Synthetic Name"), QString(ACCOUNT_NAME)}));
		QCOMPARE(revealSpy.count(), 1);
		QVERIFY(revealSpy.front().at(1).toBool());

		QTest::keyClick(&row, Qt::Key_Space);
		QCOMPARE(ShownNames(row), (QStringList{QStringLiteral("Synthetic Name"), SteamForge::HiddenAccountName()}));
	}

	void KeepsTheAccountNameWithoutSteamName() {
		const QTemporaryDir directory;
		SteamForge::SteamImageCache images;
		FakeCommunityTransport transport;
		transport.AddFailure(PROFILE_PATH, SteamForge::WebApiErrorKind::NETWORK);
		SteamForge::AccountProfiles profiles(transport, images, directory.filePath(QStringLiteral("profiles.json")));
		const SteamForge::AuthenticatorRecord record = Record();
		SteamForge::AccountRow row(record, profiles);
		row.resize(ROW_SIZE);
		row.show();
		QVERIFY(QTest::qWaitForWindowExposed(&row));

		QTRY_COMPARE(transport.SentCount(), std::size_t{1});
		QCOMPARE(ShownNames(row), (QStringList{QString(ACCOUNT_NAME), SteamForge::SteamIdText(record)}));
		QTest::keyClick(&row, Qt::Key_Space);
		QCOMPARE(ShownNames(row), (QStringList{QString(ACCOUNT_NAME), SteamForge::SteamIdText(record)}));
	}
};

QTEST_MAIN(AccountRowTest)

#include "account_row_test.moc"
