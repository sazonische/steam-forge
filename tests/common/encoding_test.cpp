#include "common/encoding.h"

class EncodingTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	// RFC 4648 section 10 test vectors.
	void DecodesBase32Vectors_data() {
		QTest::addColumn<QString>("encoded");
		QTest::addColumn<QByteArray>("expected");
		QTest::newRow("empty") << QString() << QByteArray();
		QTest::newRow("f") << QStringLiteral("MY======") << QByteArray("f");
		QTest::newRow("fo") << QStringLiteral("MZXQ====") << QByteArray("fo");
		QTest::newRow("foo") << QStringLiteral("MZXW6===") << QByteArray("foo");
		QTest::newRow("foob") << QStringLiteral("MZXW6YQ=") << QByteArray("foob");
		QTest::newRow("fooba") << QStringLiteral("MZXW6YTB") << QByteArray("fooba");
		QTest::newRow("foobar") << QStringLiteral("MZXW6YTBOI======") << QByteArray("foobar");
		QTest::newRow("lower case and spaces") << QStringLiteral("mzxw 6ytb oi") << QByteArray("foobar");
	}

	void DecodesBase32Vectors() {
		QFETCH(QString, encoded);
		QFETCH(QByteArray, expected);
		QCOMPARE(SteamForge::DecodeBase32(encoded), std::optional(expected));
	}

	void EncodesBase32WithoutPadding_data() {
		QTest::addColumn<QByteArray>("bytes");
		QTest::addColumn<QString>("expected");
		QTest::newRow("empty") << QByteArray() << QString();
		QTest::newRow("f") << QByteArray("f") << QStringLiteral("MY");
		QTest::newRow("fo") << QByteArray("fo") << QStringLiteral("MZXQ");
		QTest::newRow("foo") << QByteArray("foo") << QStringLiteral("MZXW6");
		QTest::newRow("foob") << QByteArray("foob") << QStringLiteral("MZXW6YQ");
		QTest::newRow("fooba") << QByteArray("fooba") << QStringLiteral("MZXW6YTB");
		QTest::newRow("foobar") << QByteArray("foobar") << QStringLiteral("MZXW6YTBOI");
	}

	void EncodesBase32WithoutPadding() {
		QFETCH(QByteArray, bytes);
		QFETCH(QString, expected);
		QCOMPARE(SteamForge::EncodeBase32(bytes), expected);
		QCOMPARE(SteamForge::DecodeBase32(expected), std::optional(bytes));
	}

	void RejectsCharactersOutsideBase32() {
		QVERIFY(!SteamForge::DecodeBase32(u"MZXW1YTB").has_value());
		QVERIFY(!SteamForge::DecodeBase32(u"MZXW8YTB").has_value());
		QVERIFY(!SteamForge::DecodeBase32(u"MZXW+YTB").has_value());
	}

	void DecodesStrictBase64() {
		QCOMPARE(SteamForge::DecodeBase64(u"Zm9vYmFy"), std::optional(QByteArray("foobar")));
		QCOMPARE(SteamForge::DecodeBase64(u"AQIDBAUGBwgJCgsMDQ4PEBESExQ="), std::optional(QByteArray::fromHex("0102030405060708090a0b0c0d0e0f1011121314")));
	}

	void RejectsDamagedBase64() {
		QVERIFY(!SteamForge::DecodeBase64(u"Zm9v*mFy").has_value());
		QVERIFY(!SteamForge::DecodeBase64(u"Zm9vYmFyЖ").has_value());
	}
};

QTEST_GUILESS_MAIN(EncodingTest)

#include "encoding_test.moc"
