#include "platform/application_icons.h"

#include "common/constants.h"

#ifdef Q_OS_WIN
	#include <windows.h>
#endif

namespace SteamForge {

namespace {
	// Window icons are converted pixel for pixel; the platform applies display scaling itself.
	constexpr qreal UNSCALED_DEVICE_PIXEL_RATIO = 1.0;

#ifndef Q_OS_WIN
	// Window managers take the closest size: title bars the small ones, task switchers the large ones.
	constexpr std::array TITLE_BAR_ICON_EDGE_LENGTHS{16, 22, 24};
	constexpr std::array TASK_SWITCHER_ICON_EDGE_LENGTHS{32, 48, 64, 128, 256};
#endif

	QIcon CombineIconArtwork(std::span<const int> trayEdgeLengths, std::span<const int> applicationEdgeLengths) {
		QIcon combinedIcon;
		const QIcon trayIcon = TrayIcon();
		for (const int edgeLength : trayEdgeLengths) {
			combinedIcon.addPixmap(trayIcon.pixmap(QSize(edgeLength, edgeLength), UNSCALED_DEVICE_PIXEL_RATIO));
		}
		const QIcon applicationIcon = ApplicationIcon();
		for (const int edgeLength : applicationEdgeLengths) {
			combinedIcon.addPixmap(applicationIcon.pixmap(QSize(edgeLength, edgeLength), UNSCALED_DEVICE_PIXEL_RATIO));
		}
		return combinedIcon;
	}
} // namespace

QIcon ApplicationIcon() {
	return QIcon(APPLICATION_ICON_RESOURCE);
}

QIcon TrayIcon() {
	return QIcon(TRAY_ICON_RESOURCE);
}

QIcon WindowIcon() {
#ifdef Q_OS_WIN
	// Qt builds the title bar icon at SM_CXSMICON and the taskbar icon at SM_CXICON.
	const std::array titleBarIconEdgeLengths{GetSystemMetrics(SM_CXSMICON)};
	const std::array taskSwitcherIconEdgeLengths{GetSystemMetrics(SM_CXICON)};
	return CombineIconArtwork(titleBarIconEdgeLengths, taskSwitcherIconEdgeLengths);
#else
	return CombineIconArtwork(TITLE_BAR_ICON_EDGE_LENGTHS, TASK_SWITCHER_ICON_EDGE_LENGTHS);
#endif
}

} // namespace SteamForge
