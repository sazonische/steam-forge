#pragma once

namespace SteamForge {

class SystemTray final : public QObject {
	Q_OBJECT

public:
	explicit SystemTray(QObject* parent = nullptr);

	[[nodiscard]] static bool IsAvailable();
	[[nodiscard]] bool IsShown() const;
	// Windows shows notifications from it.
	[[nodiscard]] QSystemTrayIcon& Icon();

	void Show();
	void Hide();
	void RetranslateUi();

Q_SIGNALS:
	void OpenRequested();
	void QuitRequested();

private:
	void HandleActivation(QSystemTrayIcon::ActivationReason activationReason);

	QMenu trayMenu;
	QSystemTrayIcon trayIcon;
	QAction* openAction = nullptr;
	QAction* quitAction = nullptr;
};

} // namespace SteamForge
