#include "steam/guard_code.h"

namespace {
constexpr QLatin1StringView STEAM_CODE_ALPHABET{"23456789BCDFGHJKMNPQRTVWXY"};
constexpr int STEAM_CODE_LENGTH = 5;

// Synthetic secret (bytes 0x01..0x14) of the offline reference vectors; never a real account.
QByteArray SyntheticSharedSecret() {
	return QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
}

std::chrono::sys_seconds SteamTime(qint64 secondsSinceEpoch) {
	return std::chrono::sys_seconds(std::chrono::seconds(secondsSinceEpoch));
}
} // namespace

class GuardCodeTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	// Expected codes come from node-steam-totp, an independent implementation.
	void GeneratesReferenceCodes_data() {
		QTest::addColumn<qint64>("steamTime");
		QTest::addColumn<QString>("expectedCode");
		QTest::newRow("epoch") << qint64{0} << QStringLiteral("6KR59");
		QTest::newRow("last second of the first step") << qint64{29} << QStringLiteral("6KR59");
		QTest::newRow("first second of the second step") << qint64{30} << QStringLiteral("DW55B");
		QTest::newRow("current era") << qint64{1710000000} << QStringLiteral("WHK7T");
		QTest::newRow("beyond 32-bit seconds") << qint64{4294967297} << QStringLiteral("92YWK");
	}

	void GeneratesReferenceCodes() {
		QFETCH(qint64, steamTime);
		QFETCH(QString, expectedCode);
		QCOMPARE(SteamForge::GenerateGuardCode(SyntheticSharedSecret(), SteamTime(steamTime)), expectedCode);
	}

	void CodeStaysTheSameWithinOneStep() {
		const qint64 stepStart = 1710000000 - (1710000000 % 30);
		const QString firstCode = SteamForge::GenerateGuardCode(SyntheticSharedSecret(), SteamTime(stepStart));
		for (qint64 i = 1; i < 30; ++i) {
			QCOMPARE(SteamForge::GenerateGuardCode(SyntheticSharedSecret(), SteamTime(stepStart + i)), firstCode);
		}
		QVERIFY(SteamForge::GenerateGuardCode(SyntheticSharedSecret(), SteamTime(stepStart + 30)) != firstCode);
	}

	void CodesUseOnlyTheSteamAlphabet() {
		for (qint64 step = 0; step < 1000; ++step) {
			const QString code = SteamForge::GenerateGuardCode(SyntheticSharedSecret(), SteamTime(step * 30));
			QCOMPARE(code.size(), STEAM_CODE_LENGTH);
			for (const QChar character : code) {
				QVERIFY2(STEAM_CODE_ALPHABET.contains(character), qPrintable(code));
			}
		}
	}
};

QTEST_GUILESS_MAIN(GuardCodeTest)

#include "guard_code_test.moc"
