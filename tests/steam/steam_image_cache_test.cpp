#include "steam/steam_image_cache.h"

class SteamImageCacheTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void LoadsOnlyHttpsPicturesOfSteam_data() {
		QTest::addColumn<QString>("url");
		QTest::addColumn<bool>("isAllowed");
		QTest::newRow("avatar") << QStringLiteral("https://avatars.akamai.steamstatic.com/0000_full.jpg") << true;
		QTest::newRow("item picture") << QStringLiteral("https://steamcommunity-a.akamaihd.net/economy/image/abc/96fx96f") << true;
		QTest::newRow("community CDN") << QStringLiteral("https://community.cloudflare.steamstatic.com/economy/image/abc") << true;
		QTest::newRow("plain HTTP") << QStringLiteral("http://avatars.steamstatic.com/0000.jpg") << false;
		QTest::newRow("other site") << QStringLiteral("https://example.com/0000.jpg") << false;
		QTest::newRow("look-alike suffix") << QStringLiteral("https://avatars.steamstatic.com.example.com/0000.jpg") << false;
		QTest::newRow("look-alike name") << QStringLiteral("https://evilsteamstatic.com/0000.jpg") << false;
		QTest::newRow("other Akamai customer") << QStringLiteral("https://someone-a.akamaihd.net/0000.jpg") << false;
		QTest::newRow("credentials in the link") << QStringLiteral("https://user@avatars.steamstatic.com/0000.jpg") << false;
	}

	void LoadsOnlyHttpsPicturesOfSteam() {
		QFETCH(QString, url);
		QFETCH(bool, isAllowed);
		QCOMPARE(SteamForge::SteamImageCache::IsSteamImageUrl(QUrl(url)), isAllowed);
	}

	void ReturnsAPictureOnceItIsCached() {
		SteamForge::SteamImageCache cache;
		const QUrl url(QStringLiteral("https://avatars.steamstatic.com/synthetic.jpg"));
		QImage picture(4, 4, QImage::Format_RGB32);
		picture.fill(Qt::red);
		const QSignalSpy loadedSpy(&cache, &SteamForge::SteamImageCache::Loaded);
		cache.Insert(url, picture);
		QCOMPARE(loadedSpy.count(), 1);
		QCOMPARE(cache.Image(url), picture);
	}

	void CanDecodeTheFormatsSteamServes() {
		const QList<QByteArray> formats = QImageReader::supportedImageFormats();
		QVERIFY(formats.contains("png"));
		QVERIFY(formats.contains("jpeg"));
	}
};

QTEST_GUILESS_MAIN(SteamImageCacheTest)

#include "steam_image_cache_test.moc"
