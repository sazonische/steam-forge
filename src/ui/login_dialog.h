#pragma once

#include "app/steam_login.h"
#include "steam/web_api_transport.h"

namespace SteamForge {

class AccountLibrary;
class SteamClock;
class StorageEncryption;

enum class LoginPurpose {
	SIGN_IN,
	MOVE_GUARD
};

// Signs in to the account of a record; a record that is not stored yet adds a new account.
class LoginDialog final : public QDialog {
	Q_OBJECT

public:
	LoginDialog(AccountLibrary& library, const SteamClock& clock, StorageEncryption& encryption, AuthenticatorRecord loginRecord, QString title, LoginPurpose loginPurpose = LoginPurpose::SIGN_IN, QWidget* parent = nullptr);

	void reject() override;
	// The record the saved sign-in belongs to; it differs from the given one after a Steam Guard move or for a known account.
	[[nodiscard]] std::optional<QUuid> SignedInRecordId() const;
	// While Google is connected: the window of a moved Steam Guard offers a ticked box to add it to Google.
	void OfferGoogle();
	[[nodiscard]] bool AddsToGoogle() const;

Q_SIGNALS:
	// The user wants to turn on encryption from the note below the form.
	void EncryptionRequested();

protected:
	void changeEvent(QEvent* event) override;

private:
	void RetranslateUi();
	void StartLogin();
	void SubmitCode();
	void ConfirmGuardMove();
	void ResendMoveCode();
	void TogglePasswordVisibility();
	void UpdateState();
	void HandleSucceeded();
	void HandleGuardMoved();
	[[nodiscard]] QString IntroText() const;
	[[nodiscard]] QString HeadlineText() const;
	void UpdateStorageNotice();
	[[nodiscard]] QString CodePrompt(LoginStage stage) const;
	[[nodiscard]] QString StatusText() const;
	[[nodiscard]] QString FailureText(const LoginFailure& loginFailure) const;
	[[nodiscard]] static QString MoveErrorText(const WebApiError& error);

	AccountLibrary& accountLibrary;
	const SteamClock& steamClock;
	AuthenticatorRecord record;
	// The Steam name, or the account name while Steam has told none.
	QString accountTitle;
	StorageEncryption& storageEncryption;
	LoginPurpose purpose;
	bool isNewAccount = false;
	bool isGuardJustMoved = false;
	bool isGoogleOffered = false;
	bool addsToGoogle = false;
	SteamWebApiTransport transport;
	std::unique_ptr<SteamLogin> login;

	QLabel* headlineLabel = nullptr;
	QLabel* introLabel = nullptr;
	QLabel* accountNameLabel = nullptr;
	QLineEdit* accountNameEdit = nullptr;
	QLabel* passwordLabel = nullptr;
	QLineEdit* passwordEdit = nullptr;
	QAction* passwordVisibilityAction = nullptr;
	QWidget* codeRow = nullptr;
	QLabel* codeLabel = nullptr;
	QLineEdit* codeEdit = nullptr;
	QPushButton* submitCodeButton = nullptr;
	QPushButton* moveGuardButton = nullptr;
	QPushButton* resendCodeButton = nullptr;
	QFrame* noAccessCard = nullptr;
	QLabel* noAccessTitleLabel = nullptr;
	QLabel* importOptionTitleLabel = nullptr;
	QLabel* importOptionLabel = nullptr;
	QLabel* moveOptionTitleLabel = nullptr;
	QLabel* moveOptionLabel = nullptr;
	QLabel* supportOptionTitleLabel = nullptr;
	QLabel* supportOptionLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QLabel* noticeIconLabel = nullptr;
	QLabel* noticeLabel = nullptr;
	QPushButton* encryptButton = nullptr;
	QLabel* automaticCodeLabel = nullptr;
	QDialogButtonBox* buttonBox = nullptr;
	QPushButton* signInButton = nullptr;
};

} // namespace SteamForge
