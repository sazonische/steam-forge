#pragma once

namespace SteamForge {

// Standard notifications of the operating system about an account; clicking one reports the account.
// Linux uses the freedesktop notification service, which works without a tray. Windows shows the
// notifications of the tray icon, so nothing appears there without one.
class DesktopNotifications final : public QObject {
	Q_OBJECT

public:
	explicit DesktopNotifications(QSystemTrayIcon* windowsTrayIcon, QObject* parent = nullptr);

	// A null record ID belongs to no account: clicking such a notification only shows the window.
	void Show(const QString& title, const QString& text, const QUuid& recordId);

Q_SIGNALS:
	void Activated(const QUuid& recordId);

private Q_SLOTS:
	void HandleActionInvoked(uint notificationId, const QString& actionKey);
	void HandleClosed(uint notificationId, uint closeReason);

private:
	// The tray may be destroyed first when the main window closes.
	QPointer<QSystemTrayIcon> trayIcon;
	QHash<uint, QUuid> recordIdsByNotification;
	QUuid lastTrayRecordId;
};

} // namespace SteamForge
