#include "ui/steam_guard_qr_dialog.h"

#include <ZXing/ReadBarcode.h>

namespace {
constexpr QLatin1StringView STEAM_GUARD_URI{"otpauth://totp/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&issuer=Steam&digits=5&encoder=steam"};
constexpr QLatin1StringView STEAM_TYPE_URI{"otpauth://steam/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&issuer=Steam&digits=5"};
constexpr int TEST_EDGE_LENGTH = 300;

QStringList ReadQrCodes(const QImage& image) {
	const QImage grayscaleImage = image.convertToFormat(QImage::Format_Grayscale8);
	ZXing::ReaderOptions readerOptions;
	readerOptions.setFormats(ZXing::BarcodeFormat::QRCode);
	const ZXing::ImageView imageView(grayscaleImage.constBits(), grayscaleImage.width(), grayscaleImage.height(), ZXing::ImageFormat::Lum, static_cast<int>(grayscaleImage.bytesPerLine()));
	QStringList texts;
	for (const auto& barcode : ZXing::ReadBarcodes(imageView, readerOptions)) {
		texts.append(QString::fromStdString(barcode.text()));
	}
	return texts;
}

QPushButton* FindButton(const QWidget& window, const QString& text) {
	const QList<QPushButton*> buttons = window.findChildren<QPushButton*>();
	const auto found = std::ranges::find_if(buttons, [&text](const QPushButton* button) {
		return button->text() == text;
	});
	return found == buttons.end() ? nullptr : *found;
}

SteamForge::SteamGuardLinks TestLinks() {
	return SteamForge::SteamGuardLinks{.steamEncoderLink = QString(STEAM_GUARD_URI), .steamTypeLink = QString(STEAM_TYPE_URI)};
}
} // namespace

class SteamGuardQrTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ReadsBackTheSameLink() {
		const QImage image = SteamForge::QrCodeImage(QString(STEAM_GUARD_URI), TEST_EDGE_LENGTH);
		QVERIFY(!image.isNull());
		QCOMPARE(ReadQrCodes(image), QStringList{QString(STEAM_GUARD_URI)});
	}

	void HasTheSameSizeForBothLinks() {
		const QImage encoderImage = SteamForge::QrCodeImage(QString(STEAM_GUARD_URI), TEST_EDGE_LENGTH);
		const QImage steamTypeImage = SteamForge::QrCodeImage(QString(STEAM_TYPE_URI), TEST_EDGE_LENGTH);
		QCOMPARE(encoderImage.size(), QSize(TEST_EDGE_LENGTH, TEST_EDGE_LENGTH));
		QCOMPARE(steamTypeImage.size(), QSize(TEST_EDGE_LENGTH, TEST_EDGE_LENGTH));
		QCOMPARE(ReadQrCodes(steamTypeImage), QStringList{QString(STEAM_TYPE_URI)});
	}

	void KeepsTheQuietZoneWhite() {
		const QImage image = SteamForge::QrCodeImage(QString(STEAM_GUARD_URI), TEST_EDGE_LENGTH);
		QCOMPARE(image.pixelColor(0, 0), QColor(Qt::white));
		QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1), QColor(Qt::white));
	}

	void HidesTheCodeUntilShown() {
		SteamForge::SteamGuardQrDialog dialog(QStringLiteral("synthetic_user"), TestLinks());
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		// Not even a picture of the window gives the code away.
		QVERIFY(ReadQrCodes(dialog.grab().toImage()).isEmpty());

		QPushButton* showButton = FindButton(dialog, QStringLiteral("Show QR code"));
		QPushButton* hideButton = FindButton(dialog, QStringLiteral("Hide QR code"));
		if (showButton == nullptr || hideButton == nullptr) {
			QFAIL("The buttons that show and hide the code are missing.");
		}
		QVERIFY(!hideButton->isVisible());
		showButton->click();
		QCOMPARE(ReadQrCodes(dialog.grab().toImage()), QStringList{QString(STEAM_GUARD_URI)});
		QVERIFY(!showButton->isVisible());

		hideButton->click();
		QVERIFY(ReadQrCodes(dialog.grab().toImage()).isEmpty());
		QVERIFY(showButton->isVisible());
	}

	void ShowsTheLinkOfTheChosenApps() {
		SteamForge::SteamGuardQrDialog dialog(QStringLiteral("synthetic_user"), TestLinks());
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		QPushButton* steamTypeTab = FindButton(dialog, QStringLiteral("Aegis, Ente, 2FAS for Android"));
		QPushButton* copyLinkButton = FindButton(dialog, QStringLiteral("Copy link"));
		QPushButton* showButton = FindButton(dialog, QStringLiteral("Show QR code"));
		if (steamTypeTab == nullptr || copyLinkButton == nullptr || showButton == nullptr) {
			QFAIL("The app tab, the copy button or the show button is missing.");
		}

		showButton->click();
		QCOMPARE(ReadQrCodes(dialog.grab().toImage()), QStringList{QString(STEAM_GUARD_URI)});
		steamTypeTab->click();
		// A shown code stays shown and changes to the link of the chosen apps.
		QCOMPARE(ReadQrCodes(dialog.grab().toImage()), QStringList{QString(STEAM_TYPE_URI)});
		copyLinkButton->click();
		QCOMPARE(QGuiApplication::clipboard()->text(), QString(STEAM_TYPE_URI));
	}
};

QTEST_MAIN(SteamGuardQrTest)

#include "steam_guard_qr_test.moc"
