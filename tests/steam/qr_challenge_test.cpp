#include "steam/qr_challenge.h"

#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>

namespace {
constexpr int QR_MODULE_SIZE = 6;
constexpr int QR_MARGIN_MODULES = 4;

QImage QrImage(const QString& text) {
	ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::QRCode);
	writer.setMargin(QR_MARGIN_MODULES);
	const ZXing::BitMatrix matrix = writer.encode(text.toStdString(), 0, 0);
	QImage image(matrix.width() * QR_MODULE_SIZE, matrix.height() * QR_MODULE_SIZE, QImage::Format_RGB32);
	image.fill(Qt::white);
	for (int row = 0; row < matrix.height(); ++row) {
		for (int column = 0; column < matrix.width(); ++column) {
			if (matrix.get(column, row)) {
				for (int i = 0; i < QR_MODULE_SIZE; ++i) {
					for (int j = 0; j < QR_MODULE_SIZE; ++j) {
						image.setPixel((column * QR_MODULE_SIZE) + j, (row * QR_MODULE_SIZE) + i, qRgb(0, 0, 0));
					}
				}
			}
		}
	}
	return image;
}

QImage SideBySide(const QImage& left, const QImage& right) {
	QImage combined(left.width() + right.width(), std::max(left.height(), right.height()), QImage::Format_RGB32);
	combined.fill(Qt::white);
	QPainter painter(&combined);
	painter.drawImage(0, 0, left);
	painter.drawImage(left.width(), 0, right);
	return combined;
}
} // namespace

class QrChallengeTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ParsesChallengeUrls_data() {
		QTest::addColumn<QString>("url");
		QTest::addColumn<int>("version");
		QTest::addColumn<quint64>("clientId");
		QTest::newRow("plain") << QStringLiteral("https://s.team/q/1/9007199254740993") << 1 << quint64{9007199254740993};
		QTest::newRow("with query") << QStringLiteral("https://s.team/q/1/42?x=1") << 1 << quint64{42};
		QTest::newRow("http") << QStringLiteral("http://s.team/q/1/42") << 1 << quint64{42};
		QTest::newRow("largest client id") << QStringLiteral("https://s.team/q/1/18446744073709551615") << 1 << quint64{18446744073709551615ULL};
	}

	void ParsesChallengeUrls() {
		QFETCH(QString, url);
		QFETCH(int, version);
		QFETCH(quint64, clientId);
		const std::optional<SteamForge::QrChallenge> challenge = SteamForge::ParseQrChallengeUrl(url);
		QVERIFY(challenge.has_value());
		QCOMPARE(challenge->version, version);
		QCOMPARE(challenge->clientId, clientId);
	}

	void RejectsOtherUrls_data() {
		QTest::addColumn<QString>("url");
		QTest::newRow("other host") << QStringLiteral("https://example.com/q/1/42");
		QTest::newRow("other path") << QStringLiteral("https://s.team/a/1/42");
		QTest::newRow("client id overflow") << QStringLiteral("https://s.team/q/1/18446744073709551616");
		QTest::newRow("text") << QStringLiteral("otpauth://totp/Steam:x?secret=AAAA");
	}

	void RejectsOtherUrls() {
		QFETCH(QString, url);
		QVERIFY(!SteamForge::ParseQrChallengeUrl(url).has_value());
	}

	// Signatures of the offline reference set: node-steam-session LoginApprover layout, recomputed in Python.
	void SignsLikeTheReferenceImplementation_data() {
		QTest::addColumn<quint64>("clientId");
		QTest::addColumn<quint64>("steamId");
		QTest::addColumn<QByteArray>("expectedSignature");
		QTest::newRow("above 2^53") << quint64{9007199254740993} << quint64{76561198000000000} << QByteArray::fromBase64("e2S+V3Rx6Yp8WV91qwnoAeKZp+9/+ex+UdOkyL6RHj0=");
		QTest::newRow("largest client id") << quint64{18446744073709551615ULL} << quint64{76561198000000001} << QByteArray::fromBase64("rShJvBRX2K86J+9tJ8DcckLXAyf1xHgY5zCOPJCz/g4=");
		QTest::newRow("smallest client id") << quint64{1} << quint64{76561198000000002} << QByteArray::fromBase64("sLIsOqjZSaENvGk33+oX9pjqHoh76jZatFAccFK5Bs8=");
	}

	void SignsLikeTheReferenceImplementation() {
		QFETCH(quint64, clientId);
		QFETCH(quint64, steamId);
		QFETCH(QByteArray, expectedSignature);
		const QByteArray sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
		QCOMPARE(SteamForge::SignQrChallenge({.version = 1, .clientId = clientId}, steamId, sharedSecret), expectedSignature);
	}

	void FindsSteamCodesAndIgnoresOthers() {
		const QImage screen = SideBySide(QrImage(QStringLiteral("https://s.team/q/1/123456789")), QrImage(QStringLiteral("https://example.com/not-steam")));
		const std::vector<SteamForge::QrChallenge> challenges = SteamForge::FindQrChallenges(screen);
		QCOMPARE(challenges.size(), std::size_t{1});
		QCOMPARE(challenges.front().clientId, quint64{123456789});
	}

	void FindsEveryDistinctSteamCode() {
		const QImage screen = SideBySide(QrImage(QStringLiteral("https://s.team/q/1/111")), QrImage(QStringLiteral("https://s.team/q/1/222")));
		QCOMPARE(SteamForge::FindQrChallenges(screen).size(), std::size_t{2});
	}

	void FindsNothingOnAnEmptyScreen() {
		QImage screen(400, 300, QImage::Format_RGB32);
		screen.fill(Qt::darkGray);
		QVERIFY(SteamForge::FindQrChallenges(screen).empty());
	}
};

QTEST_MAIN(QrChallengeTest)

#include "qr_challenge_test.moc"
