#include "ui/system_tray.h"

#include "platform/application_icons.h"

namespace SteamForge {

SystemTray::SystemTray(QObject* parent) :
	QObject(parent) {
	trayIcon.setIcon(TrayIcon());
	trayIcon.setToolTip(QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion()));

	const QAction* openAction = trayMenu.addAction(tr("Open SteamForge"));
	trayMenu.addSeparator();
	const QAction* quitAction = trayMenu.addAction(tr("Exit"));
	connect(openAction, &QAction::triggered, this, &SystemTray::OpenRequested);
	connect(quitAction, &QAction::triggered, this, &SystemTray::QuitRequested);

	trayIcon.setContextMenu(&trayMenu);
	connect(&trayIcon, &QSystemTrayIcon::activated, this, &SystemTray::HandleActivation);
}

bool SystemTray::IsAvailable() {
	return QSystemTrayIcon::isSystemTrayAvailable();
}

bool SystemTray::IsShown() const {
	return trayIcon.isVisible() && IsAvailable();
}

void SystemTray::Show() {
	trayIcon.show();
}

void SystemTray::Hide() {
	trayIcon.hide();
}

void SystemTray::HandleActivation(QSystemTrayIcon::ActivationReason activationReason) {
	if (activationReason == QSystemTrayIcon::Trigger || activationReason == QSystemTrayIcon::DoubleClick) {
		Q_EMIT OpenRequested();
	}
}

} // namespace SteamForge
