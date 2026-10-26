#include "ui/login_dialog.h"

#include "app/account_library.h"
#include "app/storage_encryption.h"
#include "ui/record_presentation.h"
#include "ui/success_dialog.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView LOCK_ICON_RESOURCE{":/styles/lock.svg"};
	constexpr QLatin1StringView LOCK_OPEN_ICON_RESOURCE{":/styles/lock-open.svg"};
	constexpr QSize NOTICE_ICON_SIZE{16, 16};
	constexpr int NOTICE_ICON_SPACING = 8;
	constexpr int NOTICE_LINE_SPACING = 2;
	constexpr QLatin1StringView SHOW_PASSWORD_ICON_RESOURCE{":/styles/eye.svg"};
	constexpr QLatin1StringView HIDE_PASSWORD_ICON_RESOURCE{":/styles/eye-off.svg"};
	constexpr int MINIMUM_DIALOG_WIDTH = 480;
	constexpr int DIALOG_SPACING = 8;
	constexpr int SECTION_SPACING = 12;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins CARD_MARGINS{16, 12, 16, 14};
	constexpr int OPTION_SPACING = 8;
	constexpr int OPTION_LINE_SPACING = 2;

	bool IsBusy(LoginStage stage) {
		return stage == LoginStage::STARTING || stage == LoginStage::FINISHING;
	}

	std::pair<QLabel*, QLabel*> CreateOptionLabels(QWidget& parent) {
		auto* titleLabel = new QLabel(&parent);
		titleLabel->setWordWrap(true);
		auto* descriptionLabel = new QLabel(&parent);
		descriptionLabel->setObjectName(SECONDARY_TEXT_STYLE);
		descriptionLabel->setWordWrap(true);
		return {titleLabel, descriptionLabel};
	}

	bool IsAwaitingCode(LoginStage stage) {
		return stage == LoginStage::AWAITING_EMAIL_CODE || stage == LoginStage::AWAITING_DEVICE_CODE || stage == LoginStage::AWAITING_MOVE_CODE;
	}
} // namespace

LoginDialog::LoginDialog(AccountLibrary& library, const SteamClock& clock, StorageEncryption& encryption, AuthenticatorRecord loginRecord, QString title, LoginPurpose loginPurpose, QWidget* parent) :
	QDialog(parent),
	accountLibrary(library),
	steamClock(clock),
	record(std::move(loginRecord)),
	accountTitle(std::move(title)),
	storageEncryption(encryption),
	purpose(loginPurpose),
	isNewAccount(library.FindRecord(record.id) == nullptr) {
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);

	headlineLabel = new QLabel(this);
	headlineLabel->setObjectName(PAGE_TITLE_STYLE);
	headlineLabel->setTextFormat(Qt::PlainText);
	headlineLabel->setWordWrap(true);
	introLabel = new QLabel(this);
	introLabel->setObjectName(SECONDARY_TEXT_STYLE);
	introLabel->setWordWrap(true);
	accountNameLabel = new QLabel(this);
	MakeSectionLabel(*accountNameLabel);
	// A record imported from an otpauth link knows only its label, which usually is the account name.
	accountNameEdit = new QLineEdit(record.account.accountName.isEmpty() ? record.label : record.account.accountName, this);
	passwordLabel = new QLabel(this);
	MakeSectionLabel(*passwordLabel);
	passwordEdit = new QLineEdit(this);
	passwordEdit->setEchoMode(QLineEdit::Password);
	passwordVisibilityAction = passwordEdit->addAction(QIcon(SHOW_PASSWORD_ICON_RESOURCE), QLineEdit::TrailingPosition);

	codeRow = new QWidget(this);
	codeLabel = new QLabel(codeRow);
	codeLabel->setWordWrap(true);
	codeEdit = new QLineEdit(codeRow);
	submitCodeButton = new QPushButton(codeRow);
	SetButtonRole(*submitCodeButton, ButtonRole::PRIMARY);
	resendCodeButton = new QPushButton(codeRow);
	MakeCompact(*resendCodeButton);
	auto* codeInputLayout = new QHBoxLayout();
	codeInputLayout->addWidget(codeEdit, 1);
	codeInputLayout->addWidget(submitCodeButton);
	auto* codeActionsLayout = new QHBoxLayout();
	codeActionsLayout->addWidget(resendCodeButton);
	codeActionsLayout->addStretch();
	auto* codeRowLayout = new QVBoxLayout(codeRow);
	codeRowLayout->setContentsMargins(0, 0, 0, 0);
	codeRowLayout->addWidget(codeLabel);
	codeRowLayout->addLayout(codeInputLayout);
	codeRowLayout->addLayout(codeActionsLayout);
	codeRow->hide();

	// The ways out when the code is unavailable, each its own block, as in the Steam mobile application.
	noAccessCard = new QFrame(this);
	noAccessCard->setObjectName(CARD_STYLE);
	noAccessTitleLabel = new QLabel(noAccessCard);
	MakeSectionLabel(*noAccessTitleLabel);
	std::tie(importOptionTitleLabel, importOptionLabel) = CreateOptionLabels(*noAccessCard);
	std::tie(moveOptionTitleLabel, moveOptionLabel) = CreateOptionLabels(*noAccessCard);
	moveGuardButton = new QPushButton(noAccessCard);
	MakeCompact(*moveGuardButton);
	std::tie(supportOptionTitleLabel, supportOptionLabel) = CreateOptionLabels(*noAccessCard);
	auto* moveButtonLayout = new QHBoxLayout();
	moveButtonLayout->addWidget(moveGuardButton);
	moveButtonLayout->addStretch();
	auto* noAccessLayout = new QVBoxLayout(noAccessCard);
	noAccessLayout->setContentsMargins(CARD_MARGINS);
	noAccessLayout->setSpacing(OPTION_LINE_SPACING);
	noAccessLayout->addWidget(noAccessTitleLabel);
	noAccessLayout->addSpacing(OPTION_SPACING);
	noAccessLayout->addWidget(importOptionTitleLabel);
	noAccessLayout->addWidget(importOptionLabel);
	noAccessLayout->addSpacing(OPTION_SPACING);
	noAccessLayout->addWidget(moveOptionTitleLabel);
	noAccessLayout->addWidget(moveOptionLabel);
	noAccessLayout->addLayout(moveButtonLayout);
	noAccessLayout->addSpacing(OPTION_SPACING);
	noAccessLayout->addWidget(supportOptionTitleLabel);
	noAccessLayout->addWidget(supportOptionLabel);
	noAccessCard->hide();

	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	statusLabel->setTextFormat(Qt::PlainText);
	statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	auto* noticeRow = new QWidget(this);
	noticeIconLabel = new QLabel(noticeRow);
	noticeIconLabel->setAlignment(Qt::AlignTop);
	noticeLabel = new QLabel(noticeRow);
	noticeLabel->setObjectName(SECONDARY_TEXT_STYLE);
	noticeLabel->setWordWrap(true);
	encryptButton = new QPushButton(noticeRow);
	encryptButton->setObjectName(LINK_BUTTON_STYLE);
	encryptButton->setCursor(Qt::PointingHandCursor);
	auto* noticeTextLayout = new QVBoxLayout();
	noticeTextLayout->setContentsMargins(0, 0, 0, 0);
	noticeTextLayout->setSpacing(NOTICE_LINE_SPACING);
	noticeTextLayout->addWidget(noticeLabel);
	noticeTextLayout->addWidget(encryptButton, 0, Qt::AlignLeft);
	auto* noticeLayout = new QHBoxLayout(noticeRow);
	noticeLayout->setContentsMargins(0, 0, 0, 0);
	noticeLayout->setSpacing(NOTICE_ICON_SPACING);
	noticeLayout->addWidget(noticeIconLabel, 0, Qt::AlignTop);
	noticeLayout->addLayout(noticeTextLayout, 1);
	buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	signInButton = buttonBox->addButton(QString(), QDialogButtonBox::AcceptRole);
	SetButtonRole(*signInButton, ButtonRole::PRIMARY);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(headlineLabel);
	dialogLayout->addWidget(introLabel);
	dialogLayout->addSpacing(SECTION_SPACING);
	dialogLayout->addWidget(accountNameLabel);
	dialogLayout->addWidget(accountNameEdit);
	dialogLayout->addWidget(passwordLabel);
	dialogLayout->addWidget(passwordEdit);
	dialogLayout->addSpacing(SECTION_SPACING);
	dialogLayout->addWidget(codeRow);
	dialogLayout->addWidget(statusLabel);
	dialogLayout->addWidget(noAccessCard);
	dialogLayout->addWidget(noticeRow);
	dialogLayout->addSpacing(SECTION_SPACING);
	dialogLayout->addWidget(buttonBox);

	connect(passwordVisibilityAction, &QAction::triggered, this, &LoginDialog::TogglePasswordVisibility);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &LoginDialog::StartLogin);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &LoginDialog::reject);
	connect(submitCodeButton, &QPushButton::clicked, this, &LoginDialog::SubmitCode);
	connect(codeEdit, &QLineEdit::returnPressed, this, &LoginDialog::SubmitCode);
	connect(moveGuardButton, &QPushButton::clicked, this, &LoginDialog::ConfirmGuardMove);
	connect(resendCodeButton, &QPushButton::clicked, this, &LoginDialog::ResendMoveCode);
	connect(encryptButton, &QPushButton::clicked, this, &LoginDialog::EncryptionRequested);
	connect(&storageEncryption, &StorageEncryption::StatusChanged, this, &LoginDialog::UpdateStorageNotice);

	if (accountNameEdit->text().isEmpty()) {
		accountNameEdit->setFocus();
	} else {
		passwordEdit->setFocus();
	}
	RetranslateUi();
}

void LoginDialog::reject() {
	if (login) {
		login->Cancel();
	}
	QDialog::reject();
}

std::optional<QUuid> LoginDialog::SignedInRecordId() const {
	if (!login || login->Stage() != LoginStage::SUCCEEDED) {
		return std::nullopt;
	}
	return login->Record().id;
}

void LoginDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void LoginDialog::RetranslateUi() {
	setWindowTitle(isNewAccount ? tr("Add a Steam account") : tr("Sign in to Steam"));
	headlineLabel->setText(HeadlineText());
	introLabel->setText(IntroText());
	introLabel->setVisible(!introLabel->text().isEmpty());
	accountNameLabel->setText(tr("Account name"));
	passwordLabel->setText(tr("Password"));
	encryptButton->setText(tr("Turn on encryption"));
	UpdateStorageNotice();
	submitCodeButton->setText(tr("Send code"));
	moveGuardButton->setText(tr("Move Steam Guard to SteamForge…"));
	resendCodeButton->setText(tr("Send the SMS again"));
	noAccessTitleLabel->setText(tr("No access to the Steam Guard codes?"));
	importOptionTitleLabel->setText(tr("You keep the authenticator file"));
	importOptionLabel->setText(tr("Cancel and import the .maFile or twofactor_<SteamID>.json of this account. Then the code is entered automatically, and nothing changes on the account."));
	moveOptionTitleLabel->setText(tr("You have the phone number of the account"));
	moveOptionLabel->setText(tr("Steam sends an SMS code and moves Steam Guard to SteamForge, as the Steam mobile application does on a new phone. The current authenticator stops working, and Steam may hold trades and market listings for a few days."));
	supportOptionTitleLabel->setText(tr("Neither the file nor the phone number"));
	supportOptionLabel->setText(tr("If you are still signed in to Steam on another device, remove Steam Guard there: in the Steam mobile application, or in the Steam client under Settings, Account, Manage Steam Guard. Then sign in here with the email code and set up Steam Guard in SteamForge. Without such a device, use the recovery code or Steam Support at help.steampowered.com. After a removal Steam restricts trades and the market for 15 days."));
	signInButton->setText(tr("Sign in"));
	buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
	const bool isPasswordVisible = passwordEdit->echoMode() == QLineEdit::Normal;
	passwordVisibilityAction->setToolTip(isPasswordVisible ? tr("Hide password") : tr("Show password"));
	UpdateState();
}

void LoginDialog::StartLogin() {
	if (accountNameEdit->text().trimmed().isEmpty() || passwordEdit->text().isEmpty()) {
		statusLabel->setObjectName(ERROR_TEXT_STYLE);
		statusLabel->setText(tr("Enter the account name and the password."));
		statusLabel->style()->polish(statusLabel);
		statusLabel->show();
		return;
	}
	login = std::make_unique<SteamLogin>(transport, steamClock, accountLibrary, record);
	connect(login.get(), &SteamLogin::StageChanged, this, &LoginDialog::UpdateState);
	connect(login.get(), &SteamLogin::Succeeded, this, &LoginDialog::HandleSucceeded);
	connect(login.get(), &SteamLogin::GuardMoved, this, &LoginDialog::HandleGuardMoved, Qt::QueuedConnection);
	isGuardJustMoved = false;
	QString password = passwordEdit->text();
	passwordEdit->clear();
	login->Start(accountNameEdit->text(), std::move(password));
}

void LoginDialog::SubmitCode() {
	if (!login || codeEdit->text().trimmed().isEmpty()) {
		return;
	}
	login->SubmitCode(codeEdit->text());
	codeEdit->clear();
}

void LoginDialog::ConfirmGuardMove() {
	if (!login || !login->CanMoveGuard()) {
		return;
	}
	QMessageBox confirmation(QMessageBox::Warning, tr("Move Steam Guard"), tr("Move Steam Guard of this account to SteamForge?"), QMessageBox::NoButton, this);
	confirmation.setInformativeText(tr("Steam sends an SMS code to the phone number of the account. After the move the current authenticator of the account stops working, whether it is the Steam mobile application, a .maFile or another one, and SteamForge becomes the authenticator with new secrets. The new recovery code is shown at the end."));
	const QPushButton* moveButton = confirmation.addButton(tr("Send SMS code"), QMessageBox::AcceptRole);
	confirmation.setDefaultButton(confirmation.addButton(QMessageBox::Cancel));
	confirmation.exec();
	if (confirmation.clickedButton() == moveButton && login) {
		login->StartGuardMove();
	}
}

void LoginDialog::ResendMoveCode() {
	if (login) {
		login->StartGuardMove();
	}
}

void LoginDialog::TogglePasswordVisibility() {
	const bool willBeVisible = passwordEdit->echoMode() == QLineEdit::Password;
	passwordEdit->setEchoMode(willBeVisible ? QLineEdit::Normal : QLineEdit::Password);
	passwordVisibilityAction->setIcon(QIcon(willBeVisible ? HIDE_PASSWORD_ICON_RESOURCE : SHOW_PASSWORD_ICON_RESOURCE));
	passwordVisibilityAction->setToolTip(willBeVisible ? tr("Hide password") : tr("Show password"));
}

void LoginDialog::UpdateState() {
	const LoginStage stage = login ? login->Stage() : LoginStage::IDLE;
	const bool canEditCredentials = stage == LoginStage::IDLE || stage == LoginStage::FAILED || stage == LoginStage::CANCELLED;
	accountNameEdit->setEnabled(canEditCredentials);
	passwordEdit->setEnabled(canEditCredentials);
	signInButton->setVisible(canEditCredentials);
	codeRow->setVisible(IsAwaitingCode(stage));
	codeLabel->setText(CodePrompt(stage));
	resendCodeButton->setVisible(stage == LoginStage::AWAITING_MOVE_CODE);
	const bool isDeviceCodeStep = stage == LoginStage::AWAITING_DEVICE_CODE;
	const bool canImportInstead = isDeviceCodeStep && !DetermineCapabilities(login->Record()).canGenerateCodes;
	const bool canMove = login && login->CanMoveGuard();
	noAccessCard->setVisible(isDeviceCodeStep);
	importOptionTitleLabel->setVisible(canImportInstead);
	importOptionLabel->setVisible(canImportInstead);
	moveOptionTitleLabel->setVisible(canMove);
	moveOptionLabel->setVisible(canMove);
	moveGuardButton->setVisible(canMove);
	if (IsAwaitingCode(stage)) {
		codeEdit->setFocus();
	}

	const bool isCodeProblem = login && IsAwaitingCode(stage) && (login->IsCodeRejected() || login->MoveError().has_value());
	const bool isFailure = stage == LoginStage::FAILED || isCodeProblem;
	statusLabel->setObjectName(isFailure ? ERROR_TEXT_STYLE : SECONDARY_TEXT_STYLE);
	statusLabel->style()->unpolish(statusLabel);
	statusLabel->style()->polish(statusLabel);
	statusLabel->setText(StatusText());
	statusLabel->setVisible(!statusLabel->text().isEmpty());
	setCursor(IsBusy(stage) ? Qt::BusyCursor : Qt::ArrowCursor);
	FitHeightToContents(*this);
}

void LoginDialog::HandleSucceeded() {
	accept();
}

void LoginDialog::HandleGuardMoved() {
	if (!login || !login->MovedRecord().has_value()) {
		return;
	}
	record = *login->MovedRecord();
	SuccessDialog notice(tr("Steam Guard moved"), tr("Steam Guard of %1 is now in SteamForge. One step is left: enter the password once more, and the code is entered automatically.").arg(accountTitle), this);
	notice.SetKeptValue(tr("New recovery code"), record.secrets.revocationCode, tr("Write it down and keep it apart from this computer: with it Steam Guard can be removed from the account if SteamForge is lost. The account details show it as well."));
	notice.exec();

	// Called through a queued connection, so the finished sign-in is no longer running when it is released.
	login.reset();
	isNewAccount = false;
	isGuardJustMoved = true;
	passwordEdit->setFocus();
	RetranslateUi();
}

QString LoginDialog::HeadlineText() const {
	if (isNewAccount) {
		return tr("Sign in to Steam");
	}
	if (purpose == LoginPurpose::MOVE_GUARD) {
		return tr("Move Steam Guard");
	}
	return tr("Sign in to %1").arg(accountTitle);
}

QString LoginDialog::IntroText() const {
	// A new account needs no explanation: the headline says it all.
	if (isNewAccount) {
		return {};
	}
	if (purpose == LoginPurpose::MOVE_GUARD) {
		return tr("Sign in to %1. When Steam asks for the Steam Guard code, choose \"Move Steam Guard to SteamForge\": Steam sends an SMS code to the phone of the account.").arg(accountTitle);
	}
	if (DetermineCapabilities(record).canGenerateCodes) {
		return tr("The Steam Guard code is entered automatically.");
	}
	return tr("If Steam asks for a Steam Guard code, enter it from the Steam mobile application or move Steam Guard to SteamForge.");
}

void LoginDialog::UpdateStorageNotice() {
	const EncryptionStatus status = storageEncryption.Status();
	const bool isEncrypted = status == EncryptionStatus::ON;
	noticeIconLabel->setPixmap(QIcon(isEncrypted ? LOCK_ICON_RESOURCE : LOCK_OPEN_ICON_RESOURCE).pixmap(NOTICE_ICON_SIZE));
	if (isEncrypted) {
		noticeLabel->setText(tr("The password is not saved. The Steam sign-in is kept on this computer encrypted."));
	} else if (status == EncryptionStatus::KEY_UNAVAILABLE || status == EncryptionStatus::LOCKED) {
		noticeLabel->setText(tr("The password is not saved."));
	} else {
		noticeLabel->setText(tr("The password is not saved. The Steam sign-in is kept on this computer without encryption."));
	}
	encryptButton->setVisible(status == EncryptionStatus::OFF);
	FitHeightToContents(*this);
}

QString LoginDialog::CodePrompt(LoginStage stage) const {
	switch (stage) {
		case LoginStage::AWAITING_EMAIL_CODE:
			return tr("Steam sent a code to the email address at %1. Enter it here.").arg(login->EmailDomain());
		case LoginStage::AWAITING_DEVICE_CODE:
			return tr("Steam asks for the Steam Guard code of this account. Enter it from the Steam mobile application or from another authenticator of the account.");
		case LoginStage::AWAITING_MOVE_CODE:
			return tr("Steam sent an SMS code to the phone number of the account. Enter it to move Steam Guard to SteamForge.");
		default:
			return {};
	}
}

QString LoginDialog::StatusText() const {
	if (!login) {
		return isGuardJustMoved ? tr("Steam Guard is now in SteamForge. Enter the password once more to sign in, and the Steam Guard code is entered automatically.") : QString();
	}
	switch (login->Stage()) {
		case LoginStage::IDLE:
		case LoginStage::SUCCEEDED:
		case LoginStage::GUARD_MOVED:
		case LoginStage::CANCELLED:
			return {};
		case LoginStage::STARTING:
			return tr("Signing in to Steam…");
		case LoginStage::AWAITING_EMAIL_CODE:
			if (login->IsCodeRejected()) {
				return tr("Steam did not accept this code. Check the latest email from Steam.");
			}
			return login->IsAwaitingConfirmationToo() ? tr("You can also confirm the sign-in with the link in the email.") : QString();
		case LoginStage::AWAITING_DEVICE_CODE:
			if (login->MoveError().has_value()) {
				return MoveErrorText(*login->MoveError());
			}
			if (login->IsCodeRejected() && DetermineCapabilities(login->Record()).canGenerateCodes) {
				return tr("Steam did not accept the code. This authenticator may no longer be active on the account: enter a code from the device that has it.");
			}
			if (login->IsCodeRejected()) {
				return tr("Steam did not accept the code. Enter the current one: Steam Guard codes change every 30 seconds.");
			}
			return login->IsAwaitingConfirmationToo() ? tr("You can also approve the sign-in in the Steam mobile application.") : QString();
		case LoginStage::AWAITING_MOVE_CODE:
			if (login->MoveError().has_value()) {
				return MoveErrorText(*login->MoveError());
			}
			return login->IsCodeRejected() ? tr("Steam did not accept the SMS code. Check it and enter it again.") : QString();
		case LoginStage::AWAITING_CONFIRMATION:
			return tr("Approve the sign-in in the Steam mobile application or with the link Steam sent by email. Waiting for up to three minutes.");
		case LoginStage::FINISHING:
			return tr("Receiving the sign-in tokens…");
		case LoginStage::FAILED:
			return login->Failure().has_value() ? FailureText(*login->Failure()) : QString();
	}
	return {};
}

QString LoginDialog::FailureText(const LoginFailure& loginFailure) const {
	switch (loginFailure.kind) {
		case LoginFailureKind::WRONG_CREDENTIALS:
			return tr("The account name or the password is wrong.");
		case LoginFailureKind::RATE_LIMITED:
			return tr("Steam received too many sign-in attempts. Wait a while and try again.");
		case LoginFailureKind::WRONG_ACCOUNT:
			return tr("This account name belongs to another Steam account than this record. Nothing was saved.");
		case LoginFailureKind::TIMED_OUT:
			return tr("The sign-in was not confirmed in time. Try again.");
		case LoginFailureKind::SESSION_ENDED:
			return tr("Steam ended this sign-in attempt: it was denied or it expired. Try again.");
		case LoginFailureKind::UNSUPPORTED_CONFIRMATION:
			return tr("Steam asks for a kind of confirmation SteamForge does not support yet.");
		case LoginFailureKind::INVALID_TOKENS:
			return tr("Steam returned sign-in tokens that do not belong to this account. Nothing was saved.");
		case LoginFailureKind::ENCRYPTION_FAILED:
			return tr("The password could not be encrypted with the key Steam sent.");
		case LoginFailureKind::CONNECTION:
			return tr("Steam could not be reached. Check the internet connection and try again.");
		case LoginFailureKind::STORAGE_ERROR:
			return tr("Steam accepted the sign-in, but SteamForge could not save it on this computer.");
		case LoginFailureKind::GUARD_NOT_SAVED: {
			const QString revocationCode = login->MovedRecord().has_value() ? login->MovedRecord()->secrets.revocationCode : QString();
			return tr("Steam moved Steam Guard to SteamForge, but the new secrets could not be saved on this computer. Write down the recovery code %1 now: with it Steam Guard can be removed from the account.").arg(revocationCode);
		}
		case LoginFailureKind::STEAM_ERROR:
			return DescribeWebApiError(tr("Could not sign in to Steam."), loginFailure.error);
	}
	return {};
}

QString LoginDialog::MoveErrorText(const WebApiError& error) {
	if (error.kind == WebApiErrorKind::NETWORK || error.kind == WebApiErrorKind::TIMEOUT) {
		return tr("Steam could not be reached to send the SMS code. Try again.");
	}
	return DescribeWebApiError(tr("Steam did not start moving Steam Guard."), error, tr("Moving with an SMS code works only when a phone number is linked to the account."));
}

} // namespace SteamForge
