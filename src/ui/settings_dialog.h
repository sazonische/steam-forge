#pragma once

#include "app/interface_translator.h"
#include "app/storage_encryption.h"
#include "platform/data_location.h"
#include "ui/cloud_accounts_view.h"

namespace SteamForge {

class AccountLibrary;
class AccountProfiles;
class CloudSync;
class GoogleConnection;

enum class SettingsSection {
	GENERAL,
	ENCRYPTION,
	GOOGLE,
	UPDATES
};

// Sections on the left as in the settings of Steam. The statuses at the top of the main window open the section
// they tell about; turning encryption and Google on or off happens here.
class SettingsDialog final : public QDialog {
	Q_OBJECT

public:
	// What the Google section shows of the accounts.
	struct CloudAccountsSource {
		const CloudSync& sync;
		const AccountLibrary& library;
		CloudAccountsView::AccountTitleProvider titleProvider;
		// The avatars of the accounts; without them the rows show none.
		AccountProfiles* profiles = nullptr;
	};

	SettingsDialog(std::optional<InterfaceLanguage> currentLanguage, bool checksForUpdates, DataLocation location, const StorageEncryption& encryption, GoogleConnection& google, const CloudAccountsSource& cloudAccounts, SettingsSection section, QWidget* parent = nullptr);

	void ShowSection(SettingsSection section);
	// The accounts and their ticks in the Google section; its requests go to the main window.
	[[nodiscard]] CloudAccountsView& CloudAccounts();

Q_SIGNALS:
	void LanguageSelected(std::optional<InterfaceLanguage> language);
	void UpdateChecksToggled(bool isEnabled);
	void EncryptionTurnOnRequested();
	void EncryptionTurnOffRequested();
	void UnlockRequested();
	void GoogleConnectRequested();
	void GoogleDisconnectRequested();

protected:
	void changeEvent(QEvent* event) override;

private:
	[[nodiscard]] QWidget* CreateGeneralPage();
	[[nodiscard]] QWidget* CreateEncryptionPage();
	[[nodiscard]] QWidget* CreateGooglePage(const CloudAccountsSource& cloudAccounts);
	[[nodiscard]] QWidget* CreateUpdatesPage();
	void RetranslateUi();
	void UpdateEncryptionPage();
	void UpdateGooglePage();
	void SelectLanguage(int comboIndex);
	void OpenDataFolder();
	void PressEncryptionAction();

	DataLocation dataLocation;
	const StorageEncryption& storageEncryption;
	GoogleConnection& googleConnection;

	QListWidget* navigationList = nullptr;
	QStackedWidget* pageStack = nullptr;

	QLabel* generalTitleLabel = nullptr;
	QLabel* languageSectionLabel = nullptr;
	QComboBox* languageComboBox = nullptr;

	QLabel* encryptionTitleLabel = nullptr;
	QLabel* encryptionIconLabel = nullptr;
	QLabel* encryptionHeadlineLabel = nullptr;
	QLabel* encryptionTextLabel = nullptr;
	QLabel* encryptionGoogleLabel = nullptr;
	QPushButton* encryptionActionButton = nullptr;
	QLabel* dataFolderSectionLabel = nullptr;
	QLineEdit* dataFolderEdit = nullptr;
	QPushButton* openFolderButton = nullptr;
	QLabel* portableModeLabel = nullptr;

	QLabel* googleTitleLabel = nullptr;
	QLabel* googleIconLabel = nullptr;
	QLabel* googleHeadlineLabel = nullptr;
	QLabel* googleEmailLabel = nullptr;
	QLabel* googleTextLabel = nullptr;
	QPushButton* googleConnectButton = nullptr;
	QPushButton* googleDisconnectButton = nullptr;
	CloudAccountsView* cloudAccountsView = nullptr;

	QLabel* updatesTitleLabel = nullptr;
	QCheckBox* updateChecksCheckBox = nullptr;
	QLabel* updatesNoteLabel = nullptr;

	QDialogButtonBox* buttonBox = nullptr;
};

} // namespace SteamForge
