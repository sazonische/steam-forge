#include "app/application.h"

#include "common/constants.h"
#include "common/version.h"
#include "platform/application_icons.h"
#include "ui/main_window.h"

namespace SteamForge {

Application::Application(int& argumentCount, char** argumentValues) :
	QApplication(argumentCount, argumentValues) {
	setApplicationName(APPLICATION_NAME);
	setApplicationVersion(APPLICATION_VERSION);
	setDesktopFileName(APPLICATION_ID);
	setWindowIcon(WindowIcon());

	if (!interfaceTranslator.Apply(InterfaceTranslator::DetectSystemLanguage())) {
		qWarning("Interface translations could not be loaded; falling back to English.");
	}
}

// Defined here, where MainWindow is complete, so the window is destroyed before QApplication.
Application::~Application() = default;

int Application::Run() {
	switch (singleInstanceGuard.Acquire()) {
		case InstanceAcquisition::ACQUIRED:
			break;
		case InstanceAcquisition::ACTIVATED_RUNNING_INSTANCE:
			return EXIT_SUCCESS;
		case InstanceAcquisition::UNAVAILABLE:
			QMessageBox::critical(nullptr, applicationName(), tr("SteamForge is already running, or its instance endpoint is unavailable."));
			return EXIT_FAILURE;
	}

	mainWindow = std::make_unique<MainWindow>();
	connect(&singleInstanceGuard, &SingleInstanceGuard::ActivationRequested, mainWindow.get(), &MainWindow::BringToFront);
	mainWindow->show();
	return exec();
}

} // namespace SteamForge
