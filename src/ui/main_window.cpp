#include "ui/main_window.h"

#include "common/constants.h"
#include "ui/system_tray.h"

namespace SteamForge {

namespace {
	constexpr QSize INITIAL_WINDOW_SIZE{1024, 640};
} // namespace

MainWindow::MainWindow(QWidget* parent) :
	QMainWindow(parent) {
	setWindowTitle(APPLICATION_NAME);
	resize(INITIAL_WINDOW_SIZE);

	auto* emptyStateWidget = new QWidget(this);
	auto* emptyStateLayout = new QVBoxLayout(emptyStateWidget);
	auto* emptyStateLabel = new QLabel(tr("No accounts yet"), emptyStateWidget);
	emptyStateLabel->setAlignment(Qt::AlignCenter);
	emptyStateLayout->addWidget(emptyStateLabel);

	setCentralWidget(emptyStateWidget);
	CreateSystemTray();
}

void MainWindow::BringToFront() {
	// showNormal() would also drop the maximized state.
	setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	show();
	raise();
	activateWindow();
}

void MainWindow::QuitApplication() {
	isQuitRequested = true;
	if (systemTray != nullptr) {
		systemTray->Hide();
	}
	close();
	// Quit explicitly: a stray visible dialog must not keep the process alive without a window.
	QCoreApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event) {
	if (!isQuitRequested && systemTray != nullptr && systemTray->IsShown()) {
		hide();
		event->ignore();
		return;
	}
	QMainWindow::closeEvent(event);
}

void MainWindow::CreateSystemTray() {
	if (!SystemTray::IsAvailable()) {
		return;
	}
	systemTray = new SystemTray(this);
	connect(systemTray, &SystemTray::OpenRequested, this, &MainWindow::BringToFront);
	connect(systemTray, &SystemTray::QuitRequested, this, &MainWindow::QuitApplication);
	systemTray->Show();
}

} // namespace SteamForge
