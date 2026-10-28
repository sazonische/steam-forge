#pragma once

#include "app/google_connection.h"
#include "domain/authenticator_record.h"

namespace SteamForge {

class AccountLibrary;
class AccountProfiles;
class CloudSync;
class IllustrationView;
class ScrollingRowList;

// Connects the Google account step by step: what goes to Google, the sign-in in the browser, then the recovery key
// that is written down for a new vault or typed in for an existing one. A lost key gives way to a new vault. Then
// the accounts the user ticks go into Google.
class GoogleConnectDialog final : public QDialog {
	Q_OBJECT

public:
	// Turns local encryption on first when it is off; false when the user declined.
	using EncryptionStarter = std::function<bool()>;
	// The name the account list shows for an account.
	using AccountTitleProvider = std::function<QString(const AuthenticatorRecord&)>;

	// Without profiles the account list shows no avatars.
	GoogleConnectDialog(GoogleConnection& connection, CloudSync& sync, const AccountLibrary& library, AccountTitleProvider titleProvider, AccountProfiles* profiles, EncryptionStarter turnOnEncryption, bool isEncryptionOn, QWidget* parent = nullptr);

	void reject() override;

protected:
	void changeEvent(QEvent* event) override;

private:
	enum class Step {
		INTRODUCTION,
		BROWSER,
		NEW_KEY,
		ENTER_KEY,
		SAVING,
		CHOOSE_ACCOUNTS,
		UPLOADING,
		DONE
	};

	void RetranslateUi();
	void ShowStep(Step newStep);
	void PressPrimary();
	void StartSignIn();
	void OpenSignInPage();
	void SubmitRecoveryKey();
	void CopyRecoveryKey();
	void SaveRecoveryKeyToFile();
	void ShowFailure(GoogleConnectFailure failure);
	void ConfirmVaultReplacement();
	void FillAccountChoices();
	void StartUpload();
	void CountUpload(const QUuid& recordId, bool isAdded);
	void UpdateUploadButton();
	[[nodiscard]] QString DoneText() const;
	[[nodiscard]] static QString FailureText(GoogleConnectFailure failure);

	GoogleConnection& googleConnection;
	CloudSync& cloudSync;
	const AccountLibrary& accountLibrary;
	AccountTitleProvider accountTitleProvider;
	AccountProfiles* accountProfiles = nullptr;
	EncryptionStarter encryptionStarter;
	QSet<QUuid> uploadingRecordIds;
	int uploadTotalCount = 0;
	bool isEncrypted = false;
	Step step = Step::INTRODUCTION;
	bool isNewVault = false;
	bool isReplacingVault = false;
	int uploadedCount = 0;
	int failedUploadCount = 0;
	// Google already has another Steam Guard of these accounts.
	int conflictUploadCount = 0;
	std::vector<std::pair<QUuid, QCheckBox*>> accountChoices;
	QUrl signInPageUrl;
	QString recoveryKeyText;

	IllustrationView* illustration = nullptr;
	QLabel* headlineLabel = nullptr;
	QLabel* textLabel = nullptr;
	QLabel* noticeLabel = nullptr;
	QFrame* keyCard = nullptr;
	QLabel* keyTitleLabel = nullptr;
	QLabel* keyLabel = nullptr;
	QPushButton* copyKeyButton = nullptr;
	QPushButton* saveKeyButton = nullptr;
	QCheckBox* keySavedCheckBox = nullptr;
	QLineEdit* keyEdit = nullptr;
	QPushButton* replaceVaultButton = nullptr;
	QFrame* accountsFrame = nullptr;
	ScrollingRowList* accountList = nullptr;
	QLabel* statusLabel = nullptr;
	QPushButton* primaryButton = nullptr;
	QPushButton* secondaryButton = nullptr;
};

} // namespace SteamForge
