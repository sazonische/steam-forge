#include "app/application.h"

#include "app/account_library.h"
#include "app/account_profiles.h"
#include "app/application_settings.h"
#include "app/confirmation_monitor.h"
#include "app/storage_encryption.h"
#include "app/update_check.h"
#include "common/constants.h"
#include "common/version.h"
#include "platform/application_icons.h"
#include "platform/data_location.h"
#include "platform/window_frame.h"
#include "steam/community_transport.h"
#include "steam/steam_clock.h"
#include "steam/steam_image_cache.h"
#include "ui/main_window.h"
#include "ui/theme.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView SETTINGS_FILE_NAME{"settings.ini"};
	constexpr QLatin1StringView ACCOUNTS_DIRECTORY_NAME{"accounts"};
	constexpr QLatin1StringView SESSIONS_DIRECTORY_NAME{"sessions"};
	constexpr QLatin1StringView PROFILE_CACHE_FILE_PATH{"cache/profiles.json"};
	constexpr QLatin1StringView ENCRYPTION_STATE_FILE_NAME{"encryption.json"};
} // namespace

Application::Application(int& argumentCount, char** argumentValues) :
	QApplication(argumentCount, argumentValues) {
	setApplicationName(APPLICATION_NAME);
	setApplicationVersion(APPLICATION_VERSION);
	setDesktopFileName(APPLICATION_ID);
	setWindowIcon(WindowIcon());

	// Until Run() reads the settings, messages follow the system language.
	ApplyLanguage(std::nullopt);
}

// Defined here, where the owned types are complete; members go before QApplication.
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

	ApplyTheme(*this);
	InstallWindowFrameStyling(*this);
	const DataLocation dataLocation = ResolveDataLocation();
	if (dataLocation.directoryPath.isEmpty()) {
		QMessageBox::critical(nullptr, applicationName(), tr("No folder for SteamForge data is available for this user."));
		return EXIT_FAILURE;
	}
	const QDir dataDirectory(dataLocation.directoryPath);

	settings = std::make_unique<ApplicationSettings>(dataDirectory.filePath(SETTINGS_FILE_NAME));
	ApplyLanguage(settings->Language());
	accountLibrary = std::make_unique<AccountLibrary>(AuthenticatorStore(dataDirectory.filePath(ACCOUNTS_DIRECTORY_NAME)), SessionStore(dataDirectory.filePath(SESSIONS_DIRECTORY_NAME)));
	systemKeyStore = CreateSystemKeyStore(dataDirectory.path());
	storageEncryption = std::make_unique<StorageEncryption>(*systemKeyStore, *accountLibrary, dataDirectory.filePath(ENCRYPTION_STATE_FILE_NAME));
	storageEncryption->PrepareLibrary();
	if (storageEncryption->Status() != EncryptionStatus::LOCKED) {
		accountLibrary->Load();
		if (!storageEncryption->FinishInterruptedChange().has_value()) {
			qWarning("Turning encryption on or off did not finish; it continues at the next start.");
		}
	}
	steamClock = std::make_unique<SteamClock>();
	webApiTransport = std::make_unique<SteamWebApiTransport>();
	communityTransport = std::make_unique<SteamCommunityTransport>();
	confirmationMonitor = std::make_unique<ConfirmationMonitor>(*webApiTransport, *communityTransport, *accountLibrary, *steamClock);
	steamImageCache = std::make_unique<SteamImageCache>();
	accountProfiles = std::make_unique<AccountProfiles>(*communityTransport, *steamImageCache, dataDirectory.filePath(PROFILE_CACHE_FILE_PATH));
	updateCheck = std::make_unique<UpdateCheck>(QVersionNumber::fromString(APPLICATION_VERSION), QUrl(LATEST_RELEASE_API_URL));

	mainWindow = std::make_unique<MainWindow>(*accountLibrary, *steamClock, *confirmationMonitor, *steamImageCache, *accountProfiles, *storageEncryption, *settings, dataLocation);
	connect(&singleInstanceGuard, &SingleInstanceGuard::ActivationRequested, mainWindow.get(), &MainWindow::BringToFront);
	connect(mainWindow.get(), &MainWindow::LanguageChangeRequested, this, &Application::ChangeLanguage);
	connect(mainWindow.get(), &MainWindow::UpdateChecksChangeRequested, this, &Application::ChangeUpdateChecks);
	connect(updateCheck.get(), &UpdateCheck::NewerReleaseFound, mainWindow.get(), &MainWindow::ShowAvailableUpdate);
	mainWindow->show();
	systemKeyStore->SetPromptWindow(mainWindow->winId());
	mainWindow->UnlockIfLocked();
	steamClock->Synchronize();
	confirmationMonitor->Start();
	if (settings->ChecksForUpdates()) {
		updateCheck->Start();
	}
	return exec();
}

void Application::ApplyLanguage(std::optional<InterfaceLanguage> language) {
	if (!interfaceTranslator.Apply(language.value_or(InterfaceTranslator::DetectSystemLanguage()))) {
		qWarning("Interface translations could not be loaded; falling back to English.");
	}
}

void Application::ChangeLanguage(std::optional<InterfaceLanguage> language) {
	settings->SetLanguage(language);
	ApplyLanguage(language);
	confirmationMonitor->RecheckMarketRestrictions();
}

void Application::ChangeUpdateChecks(bool isEnabled) {
	settings->SetChecksForUpdates(isEnabled);
	if (isEnabled) {
		updateCheck->Start();
	} else {
		updateCheck->Stop();
	}
}

} // namespace SteamForge
