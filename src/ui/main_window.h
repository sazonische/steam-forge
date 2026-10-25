#pragma once

namespace SteamForge {

class SystemTray;

class MainWindow final : public QMainWindow {
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);

	void BringToFront();
	void QuitApplication();

protected:
	void closeEvent(QCloseEvent* event) override;

private:
	void CreateSystemTray();

	SystemTray* systemTray = nullptr;
	bool isQuitRequested = false;
};

} // namespace SteamForge
