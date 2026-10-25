#include "common/constants.h"
#include "platform/application_icons.h"

namespace {
QImage RenderIcon(const QIcon& icon, const QSize& size) {
	return icon.pixmap(size, 1.0).toImage().convertToFormat(QImage::Format_ARGB32);
}

bool HasVisiblePixels(const QIcon& icon, int edgeLength) {
	const QImage image = icon.pixmap(QSize(edgeLength, edgeLength)).toImage().convertToFormat(QImage::Format_ARGB32);
	if (image.isNull()) {
		return false;
	}
	for (int row = 0; row < image.height(); ++row) {
		for (int column = 0; column < image.width(); ++column) {
			if (qAlpha(image.pixel(column, row)) > 0) {
				return true;
			}
		}
	}
	return false;
}
} // namespace

class ApplicationIconsTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ApplicationIconRendersAtSmallAndLargeSizes() {
		const QIcon applicationIcon = SteamForge::ApplicationIcon();
		QVERIFY(!applicationIcon.isNull());
		QVERIFY(HasVisiblePixels(applicationIcon, 16));
		QVERIFY(HasVisiblePixels(applicationIcon, 256));
	}

	void TrayIconRendersAtTraySizes() {
		const QIcon trayIcon = SteamForge::TrayIcon();
		QVERIFY(!trayIcon.isNull());
		QVERIFY(HasVisiblePixels(trayIcon, 16));
		QVERIFY(HasVisiblePixels(trayIcon, 32));
	}

	void WindowIconUsesTrayArtworkForSmallestAndApplicationIconForLargestSize() {
		const QIcon windowIcon = SteamForge::WindowIcon();
		QList<QSize> availableSizes = windowIcon.availableSizes();
		QVERIFY(availableSizes.size() >= 2);
		std::ranges::sort(availableSizes, {}, &QSize::width);

		const QSize smallestSize = availableSizes.front();
		const QSize largestSize = availableSizes.back();
		QCOMPARE(RenderIcon(windowIcon, smallestSize), RenderIcon(SteamForge::TrayIcon(), smallestSize));
		QCOMPARE(RenderIcon(windowIcon, largestSize), RenderIcon(SteamForge::ApplicationIcon(), largestSize));
		QVERIFY(RenderIcon(windowIcon, largestSize) != RenderIcon(SteamForge::TrayIcon(), largestSize));
	}

	void WindowsIconsProvideDedicatedSizes() {
#ifdef Q_OS_WIN
		QVERIFY(SteamForge::ApplicationIcon().availableSizes().size() > 1);
		QVERIFY(SteamForge::TrayIcon().availableSizes().size() > 1);
#else
		QSKIP("ICO files are used only on Windows.");
#endif
	}

	void DesktopEntryMatchesApplicationIdentity() {
		const QString desktopEntryName = SteamForge::APPLICATION_ID;
		QFile desktopFile(QStringLiteral(STEAMFORGE_SOURCE_DIRECTORY "/resources/linux/") + desktopEntryName + QStringLiteral(".desktop"));
		QVERIFY2(desktopFile.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(desktopFile.fileName()));

		const QString desktopFileText = QString::fromUtf8(desktopFile.readAll());
		QVERIFY(desktopFileText.contains(QStringLiteral("\nIcon=") + desktopEntryName + QStringLiteral("\n")));
		QVERIFY(desktopFileText.contains(QStringLiteral("\nStartupWMClass=") + SteamForge::APPLICATION_NAME + QStringLiteral("\n")));
	}
};

QTEST_MAIN(ApplicationIconsTest)

#include "application_icons_test.moc"
