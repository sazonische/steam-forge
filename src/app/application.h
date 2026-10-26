#pragma once

#include "app/interface_translator.h"
#include "app/single_instance_guard.h"

namespace SteamForge {

class AccountLibrary;
class AccountProfiles;
class StorageEncryption;
class SystemKeyStore;
class ApplicationSettings;
class ConfirmationMonitor;
class MainWindow;
class SteamClock;
class SteamImageCache;
class SteamCommunityTransport;
class SteamWebApiTransport;

class Application final : public QApplication {
	Q_OBJECT
	Q_DISABLE_COPY_MOVE(Application)

public:
	Application(int& argumentCount, char** argumentValues);
	~Application() override;

	[[nodiscard]] int Run();

private:
	void ApplyLanguage(std::optional<InterfaceLanguage> language);
	void ChangeLanguage(std::optional<InterfaceLanguage> language);

	InterfaceTranslator interfaceTranslator;
	SingleInstanceGuard singleInstanceGuard;
	// Declared in dependency order: the window is destroyed first, before the objects it refers to.
	std::unique_ptr<ApplicationSettings> settings;
	std::unique_ptr<AccountLibrary> accountLibrary;
	std::unique_ptr<SystemKeyStore> systemKeyStore;
	std::unique_ptr<StorageEncryption> storageEncryption;
	std::unique_ptr<SteamClock> steamClock;
	std::unique_ptr<SteamWebApiTransport> webApiTransport;
	std::unique_ptr<SteamCommunityTransport> communityTransport;
	std::unique_ptr<ConfirmationMonitor> confirmationMonitor;
	std::unique_ptr<SteamImageCache> steamImageCache;
	std::unique_ptr<AccountProfiles> accountProfiles;
	std::unique_ptr<MainWindow> mainWindow;
};

} // namespace SteamForge
