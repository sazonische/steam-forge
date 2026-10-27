#include "crypto/key_derivation.h"

namespace {

QByteArray ByteRange(int first, int last) {
	QByteArray bytes;
	for (int byte = first; byte <= last; ++byte) {
		bytes.append(static_cast<char>(byte));
	}
	return bytes;
}

} // namespace

class KeyDerivationTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	// RFC 5869, appendix A, test cases 1 and 3.
	void MatchesTheRfcVectors_data() {
		QTest::addColumn<QByteArray>("keyMaterial");
		QTest::addColumn<QByteArray>("salt");
		QTest::addColumn<QByteArray>("info");
		QTest::addColumn<QByteArray>("expectedHex");
		QTest::newRow("salt and info") << QByteArray(22, 0x0b) << ByteRange(0x00, 0x0c) << ByteRange(0xf0, 0xf9) << QByteArray("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865");
		QTest::newRow("empty salt and info") << QByteArray(22, 0x0b) << QByteArray() << QByteArray() << QByteArray("8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4b61a96c8");
	}

	void MatchesTheRfcVectors() {
		QFETCH(QByteArray, keyMaterial);
		QFETCH(QByteArray, salt);
		QFETCH(QByteArray, info);
		QFETCH(QByteArray, expectedHex);
		const std::optional<QByteArray> derived = SteamForge::DeriveKeyBytes(keyMaterial, salt, info, 42);
		QVERIFY(derived.has_value());
		QCOMPARE(derived->toHex(), expectedHex);
	}

	void RefusesEmptyKeyMaterialAndLength() {
		QVERIFY(!SteamForge::DeriveKeyBytes(QByteArray(), QByteArray("salt"), QByteArray("info"), 32).has_value());
		QVERIFY(!SteamForge::DeriveKeyBytes(QByteArray("key"), QByteArray("salt"), QByteArray("info"), 0).has_value());
	}
};

QTEST_GUILESS_MAIN(KeyDerivationTest)

#include "key_derivation_test.moc"
