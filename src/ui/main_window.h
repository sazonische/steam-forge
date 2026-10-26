#pragma once

#include "app/interface_translator.h"
#include "domain/authenticator_record.h"
#include "platform/data_location.h"
#include "platform/system_key_store.h"
#include "steam/confirmation_service.h"
#include "ui/login_dialog.h"

namespace SteamForge {

class AccountDetailsView;
class AccountLibrary;
class AccountProfiles;
class AccountListView;
class ApplicationSettings;
class ConfirmationMonitor;
class ConfirmationsDialog;
class DesktopNotifications;
class SteamImageCache;
class StorageEncryption;
struct EncryptionFailure;
class SteamClock;
class SystemTray;

class MainWindow final : public QMainWindow {
	Q_OBJECT

public:
	MainWindow(AccountLibrary& library, const SteamClock& clock, ConfirmationMonitor& monitor, SteamImageCache& images, AccountProfiles& profiles, StorageEncryption& encryption, ApplicationSettings& applicationSettings, DataLocation location, QWidget* parent = nullptr);

	void BringToFront();
	void QuitApplication();
	// Asks for the key right after the start when Windows Hello or a keyring password guards it.
	void UnlockIfLocked();
	// Offered in a window when this one is in front; a skipped version, older ones and one postponed in this run are not.
	void ShowAvailableUpdate(const QVersionNumber& version);

Q_SIGNALS:
	void LanguageChangeRequested(std::optional<InterfaceLanguage> language);
	void UpdateChecksChangeRequested(bool isEnabled);

protected:
	void closeEvent(QCloseEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dropEvent(QDropEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	[[nodiscard]] QWidget* CreateTopBar();
	void OfferPendingUpdate();
	[[nodiscard]] QWidget* CreateEmptyStatePage();
	[[nodiscard]] QWidget* CreateLockedPage();
	void Unlock();
	[[nodiscard]] QWidget* CreateAccountsPage();
	void CreateSystemTray();
	void RetranslateUi();
	void UpdatePages();
	void ForgetProfilesOfRemovedRecords();
	void UpdateEncryptionButton();
	void ShowEncryption();
	void OfferEncryption();
	[[nodiscard]] bool OfferEncryptionWithoutPrompt(KeyProtection missingProtection);
	void ShowEncryptionOn();
	[[nodiscard]] QString MissingProtectionText(KeyProtection protection) const;
	[[nodiscard]] QString EncryptionFailureText(const EncryptionFailure& failure) const;
	// Results of the user's own actions, shown as notifications of the system like every other event.
	void Notify(const QString& title, const QString& text);
	[[nodiscard]] QString AccountTitleFor(const AuthenticatorRecord& record);
	void UpdateLoadProblems();
	void UpdateTimeStatus();
	void ShowRecord(const QUuid& recordId);
	void ShowAddAccountMenu();
	void OpenNewAccountLogin();
	void OpenLoginDialog(const QUuid& recordId);
	void OpenGuardMoveLogin(const QUuid& recordId);
	void RunLoginDialog(const AuthenticatorRecord& loginRecord, LoginPurpose purpose);
	void OpenGuardSetupDialog(const QUuid& recordId);
	void OpenLoginApprovalDialog(const QUuid& recordId);
	void OpenConfirmationsDialog(const QUuid& recordId);
	void OpenConfirmationsFromNotification(const QUuid& recordId);
	void ConnectConfirmations();
	void UpdateConfirmationIndicators();
	void NotifyConfirmations(const QUuid& recordId, const std::vector<PendingConfirmation>& newConfirmations);
	void NotifySignInRequired(const QUuid& recordId);
	void CloseConfirmationsOfRemovedRecords();
	void ConfirmSignOut(const QUuid& recordId);
	void RefreshShownRecord(const QUuid& recordId);
	void OpenImportDialog();
	void RunImportDialog(const QStringList& filePaths);
	void OpenSettingsDialog();
	void ConfirmRemoval(const QUuid& recordId);
	void OpenRecordDataDialog(const QUuid& recordId);
	void ExportRecord(const QUuid& recordId);
	void ShowSteamGuardQr(const QUuid& recordId);
	void ShowLoadProblemDetails();

	AccountLibrary& accountLibrary;
	const SteamClock& steamClock;
	ConfirmationMonitor& confirmationMonitor;
	SteamImageCache& steamImageCache;
	AccountProfiles& accountProfiles;
	StorageEncryption& storageEncryption;
	ApplicationSettings& settings;
	DataLocation dataLocation;
	SystemTray* systemTray = nullptr;
	DesktopNotifications* desktopNotifications = nullptr;
	QHash<QUuid, QPointer<ConfirmationsDialog>> confirmationsDialogs;
	bool isQuitRequested = false;

	QLabel* topBarTitleLabel = nullptr;
	QPushButton* encryptionButton = nullptr;
	QPushButton* addAccountButton = nullptr;
	QPushButton* settingsButton = nullptr;
	QLabel* loadProblemLabel = nullptr;
	QPushButton* loadProblemDetailsButton = nullptr;
	QWidget* loadProblemBanner = nullptr;
	QVersionNumber pendingUpdateVersion;
	QVersionNumber postponedUpdateVersion;
	QStackedWidget* pageStack = nullptr;
	QWidget* emptyStatePage = nullptr;
	QWidget* lockedPage = nullptr;
	QLabel* lockedTitleLabel = nullptr;
	QLabel* lockedTextLabel = nullptr;
	QPushButton* unlockButton = nullptr;
	QWidget* accountsPage = nullptr;
	QLabel* emptyStateTitleLabel = nullptr;
	QLabel* emptyStateTextLabel = nullptr;
	QPushButton* emptyStateSignInButton = nullptr;
	QPushButton* emptyStateImportButton = nullptr;
	QMenu* addAccountMenu = nullptr;
	QAction* signInAction = nullptr;
	QAction* importAction = nullptr;
	AccountListView* accountListView = nullptr;
	AccountDetailsView* accountDetailsView = nullptr;
	QLabel* timeStatusLabel = nullptr;
};

} // namespace SteamForge
