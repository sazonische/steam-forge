#include "ui/steam_guard_qr_dialog.h"

#include <ZXing/ReadBarcode.h>

namespace {
constexpr QLatin1StringView STEAM_GUARD_URI{"otpauth://totp/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&issuer=Steam"};
constexpr int TEST_MODULE_SIZE = 4;

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
} // namespace

class SteamGuardQrTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ReadsBackTheSameLink() {
		const QImage image = SteamForge::QrCodeImage(QString(STEAM_GUARD_URI), TEST_MODULE_SIZE);
		QVERIFY(!image.isNull());
		QCOMPARE(ReadQrCodes(image), QStringList{QString(STEAM_GUARD_URI)});
	}

	void KeepsTheQuietZoneWhite() {
		const QImage image = SteamForge::QrCodeImage(QString(STEAM_GUARD_URI), TEST_MODULE_SIZE);
		QCOMPARE(image.pixelColor(0, 0), QColor(Qt::white));
		QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1), QColor(Qt::white));
	}

	void HidesTheCodeUntilShown() {
		SteamForge::SteamGuardQrDialog dialog(QStringLiteral("synthetic_user"), QString(STEAM_GUARD_URI));
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));
		// Not even a picture of the window gives the code away.
		QVERIFY(ReadQrCodes(dialog.grab().toImage()).isEmpty());

		QPushButton* showButton = FindButton(dialog, QStringLiteral("Show QR code"));
		QPushButton* hideButton = FindButton(dialog, QStringLiteral("Hide QR code"));
		QVERIFY(showButton != nullptr && hideButton != nullptr);
		QVERIFY(!hideButton->isVisible());
		showButton->click();
		QCOMPARE(ReadQrCodes(dialog.grab().toImage()), QStringList{QString(STEAM_GUARD_URI)});
		QVERIFY(!showButton->isVisible());

		hideButton->click();
		QVERIFY(ReadQrCodes(dialog.grab().toImage()).isEmpty());
		QVERIFY(showButton->isVisible());
	}
};

QTEST_MAIN(SteamGuardQrTest)

#include "steam_guard_qr_test.moc"
