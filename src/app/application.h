#pragma once

#include "app/interface_translator.h"
#include "app/single_instance_guard.h"

namespace SteamForge {

class MainWindow;

class Application final : public QApplication {
	Q_OBJECT
	Q_DISABLE_COPY_MOVE(Application)

public:
	Application(int& argumentCount, char** argumentValues);
	~Application() override;

	[[nodiscard]] int Run();

private:
	InterfaceTranslator interfaceTranslator;
	SingleInstanceGuard singleInstanceGuard;
	std::unique_ptr<MainWindow> mainWindow;
};

} // namespace SteamForge
