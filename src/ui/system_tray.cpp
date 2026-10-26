#include "ui/system_tray.h"

#include "platform/application_icons.h"

namespace SteamForge {

SystemTray::SystemTray(QObject* parent) :
	QObject(parent) {
	trayIcon.setIcon(TrayIcon());
	trayIcon.setToolTip(QStringLiteral("%1 %2").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion()));

	openAction = trayMenu.addAction(QString());
	trayMenu.addSeparator();
	quitAction = trayMenu.addAction(QString());
	connect(openAction, &QAction::triggered, this, &SystemTray::OpenRequested);
	connect(quitAction, &QAction::triggered, this, &SystemTray::QuitRequested);
	RetranslateUi();

	trayIcon.setContextMenu(&trayMenu);
	connect(&trayIcon, &QSystemTrayIcon::activated, this, &SystemTray::HandleActivation);
}

bool SystemTray::IsAvailable() {
	return QSystemTrayIcon::isSystemTrayAvailable();
}

bool SystemTray::IsShown() const {
	return trayIcon.isVisible() && IsAvailable();
}

QSystemTrayIcon& SystemTray::Icon() {
	return trayIcon;
}

void SystemTray::Show() {
	trayIcon.show();
}

void SystemTray::Hide() {
	trayIcon.hide();
}

void SystemTray::RetranslateUi() {
	openAction->setText(tr("Open SteamForge"));
	quitAction->setText(tr("Exit"));
}

void SystemTray::HandleActivation(QSystemTrayIcon::ActivationReason activationReason) {
	if (activationReason == QSystemTrayIcon::Trigger || activationReason == QSystemTrayIcon::DoubleClick) {
		Q_EMIT OpenRequested();
	}
}

} // namespace SteamForge
