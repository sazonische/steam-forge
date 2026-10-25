#pragma once

namespace SteamForge {

class SystemTray final : public QObject {
	Q_OBJECT

public:
	explicit SystemTray(QObject* parent = nullptr);

	[[nodiscard]] static bool IsAvailable();
	[[nodiscard]] bool IsShown() const;

	void Show();
	void Hide();

Q_SIGNALS:
	void OpenRequested();
	void QuitRequested();

private:
	void HandleActivation(QSystemTrayIcon::ActivationReason activationReason);

	QMenu trayMenu;
	QSystemTrayIcon trayIcon;
};

} // namespace SteamForge
