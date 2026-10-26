#include "ui/guard_setup_dialog.h"

#include "ui/illustration_view.h"
#include "ui/record_presentation.h"
#include "ui/theme.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int MINIMUM_DIALOG_WIDTH = 520;
	constexpr int DIALOG_SPACING = 12;
	constexpr int RECOVERY_CODE_PIXEL_SIZE = 26;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins CARD_MARGINS{16, 12, 16, 12};
	constexpr QLatin1StringView ILLUSTRATION_RESOURCE{":/illustrations/guard_setup.svg"};
	constexpr QLatin1StringView SUCCESS_ILLUSTRATION_RESOURCE{":/illustrations/all_set.svg"};
	constexpr QSize ILLUSTRATION_SIZE{360, 190};

	bool IsBusy(GuardSetupStage stage) {
		return stage == GuardSetupStage::ADDING || stage == GuardSetupStage::FINALIZING;
	}
} // namespace

GuardSetupDialog::GuardSetupDialog(AccountLibrary& library, const SteamClock& clock, AuthenticatorRecord setupRecord, QString title, AccountSession setupSession, QWidget* parent) :
	QDialog(parent),
	record(setupRecord),
	accountTitle(std::move(title)),
	setup(transport, clock, library, std::move(setupRecord), std::move(setupSession)) {
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);

	illustration = new IllustrationView(ILLUSTRATION_RESOURCE, ILLUSTRATION_SIZE, this);
	headlineLabel = new QLabel(this);
	headlineLabel->setObjectName(PAGE_TITLE_STYLE);
	headlineLabel->setAlignment(Qt::AlignCenter);
	headlineLabel->setWordWrap(true);
	introLabel = new QLabel(this);
	introLabel->setWordWrap(true);

	codeRow = new QWidget(this);
	codeLabel = new QLabel(codeRow);
	codeLabel->setWordWrap(true);
	codeEdit = new QLineEdit(codeRow);
	confirmCodeButton = new QPushButton(codeRow);
	SetButtonRole(*confirmCodeButton, ButtonRole::PRIMARY);
	auto* codeInputLayout = new QHBoxLayout();
	codeInputLayout->addWidget(codeEdit, 1);
	codeInputLayout->addWidget(confirmCodeButton);
	auto* codeRowLayout = new QVBoxLayout(codeRow);
	codeRowLayout->setContentsMargins(0, 0, 0, 0);
	codeRowLayout->addWidget(codeLabel);
	codeRowLayout->addLayout(codeInputLayout);

	recoveryCard = new QFrame(this);
	recoveryCard->setObjectName(CARD_STYLE);
	recoveryTitleLabel = new QLabel(recoveryCard);
	MakeSectionLabel(*recoveryTitleLabel);
	recoveryCodeLabel = new QLabel(recoveryCard);
	recoveryCodeLabel->setFont(GuardCodeFont(RECOVERY_CODE_PIXEL_SIZE));
	recoveryCodeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	recoveryHintLabel = new QLabel(recoveryCard);
	recoveryHintLabel->setWordWrap(true);
	auto* recoveryLayout = new QVBoxLayout(recoveryCard);
	recoveryLayout->setContentsMargins(CARD_MARGINS);
	recoveryLayout->addWidget(recoveryTitleLabel);
	recoveryLayout->addWidget(recoveryCodeLabel);
	recoveryLayout->addWidget(recoveryHintLabel);

	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	statusLabel->setTextFormat(Qt::PlainText);
	statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	startButton = new QPushButton(this);
	SetButtonRole(*startButton, ButtonRole::PRIMARY);
	closeButton = new QPushButton(this);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(startButton);
	buttonsLayout->addWidget(closeButton);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(illustration, 0, Qt::AlignHCenter);
	dialogLayout->addWidget(headlineLabel);
	dialogLayout->addWidget(introLabel);
	dialogLayout->addWidget(codeRow);
	dialogLayout->addWidget(recoveryCard);
	dialogLayout->addWidget(statusLabel);
	dialogLayout->addLayout(buttonsLayout);

	connect(startButton, &QPushButton::clicked, this, &GuardSetupDialog::StartSetup);
	connect(closeButton, &QPushButton::clicked, this, &GuardSetupDialog::reject);
	connect(confirmCodeButton, &QPushButton::clicked, this, &GuardSetupDialog::SubmitCode);
	connect(codeEdit, &QLineEdit::returnPressed, this, &GuardSetupDialog::SubmitCode);
	connect(&setup, &GuardSetup::StageChanged, this, &GuardSetupDialog::UpdateState);

	RetranslateUi();
}

void GuardSetupDialog::reject() {
	setup.Cancel();
	QDialog::reject();
}

void GuardSetupDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void GuardSetupDialog::showEvent(QShowEvent* event) {
	QDialog::showEvent(event);
	// A pending activation needs no new request: it continues at the activation code.
	if (!hasResumedOnOpen && record.pendingActivation.has_value()) {
		hasResumedOnOpen = true;
		setup.Start();
	}
}

void GuardSetupDialog::RetranslateUi() {
	setWindowTitle(tr("Set up Steam Guard"));
	confirmCodeButton->setText(tr("Confirm"));
	startButton->setText(tr("Set up Steam Guard"));
	recoveryTitleLabel->setText(tr("Recovery code"));
	recoveryHintLabel->setText(tr("Write it down and keep it apart from this computer. With this code Steam Guard can be removed from the account if SteamForge is lost. The account details show it as well."));
	UpdateState();
}

void GuardSetupDialog::StartSetup() {
	setup.Start();
}

void GuardSetupDialog::SubmitCode() {
	if (codeEdit->text().trimmed().isEmpty()) {
		return;
	}
	setup.SubmitActivationCode(codeEdit->text());
	codeEdit->clear();
}

void GuardSetupDialog::UpdateState() {
	const GuardSetupStage stage = setup.Stage();
	const bool isIntroduction = stage == GuardSetupStage::IDLE || stage == GuardSetupStage::ADDING;
	const bool isSuccess = stage == GuardSetupStage::SUCCEEDED;
	illustration->SetResource(isSuccess ? SUCCESS_ILLUSTRATION_RESOURCE : ILLUSTRATION_RESOURCE);
	illustration->setVisible(isIntroduction || isSuccess);
	headlineLabel->setText(isSuccess ? tr("All set") : tr("Protect your Steam account"));
	headlineLabel->setVisible(isIntroduction || isSuccess);
	introLabel->setAlignment(isIntroduction || isSuccess ? Qt::AlignHCenter : Qt::AlignLeft);
	introLabel->setText(IntroText());
	startButton->setVisible(stage == GuardSetupStage::IDLE);
	codeRow->setVisible(stage == GuardSetupStage::AWAITING_ACTIVATION_CODE || stage == GuardSetupStage::FINALIZING);
	codeRow->setEnabled(stage == GuardSetupStage::AWAITING_ACTIVATION_CODE);
	codeLabel->setText(CodePrompt());
	if (stage == GuardSetupStage::AWAITING_ACTIVATION_CODE) {
		codeEdit->setFocus();
	}

	const bool isActivatedButNotSaved = stage == GuardSetupStage::FAILED && setup.Failure().has_value() && setup.Failure()->kind == GuardSetupFailureKind::ACTIVATED_NOT_SAVED;
	recoveryCard->setVisible(stage == GuardSetupStage::SUCCEEDED || isActivatedButNotSaved);
	recoveryCodeLabel->setText(setup.Record().secrets.revocationCode);
	closeButton->setText(stage == GuardSetupStage::SUCCEEDED ? tr("Done") : tr("Close"));
	SetButtonRole(*closeButton, stage == GuardSetupStage::SUCCEEDED ? ButtonRole::PRIMARY : ButtonRole::SECONDARY);

	const bool isProblem = setup.Failure().has_value() && stage != GuardSetupStage::SUCCEEDED;
	statusLabel->setObjectName(isProblem ? ERROR_TEXT_STYLE : SECONDARY_TEXT_STYLE);
	statusLabel->style()->unpolish(statusLabel);
	statusLabel->style()->polish(statusLabel);
	statusLabel->setText(StatusText());
	statusLabel->setVisible(!statusLabel->text().isEmpty());
	setCursor(IsBusy(stage) ? Qt::BusyCursor : Qt::ArrowCursor);
	FitHeightToContents(*this);
}

QString GuardSetupDialog::IntroText() const {
	const QString& accountName = accountTitle;
	switch (setup.Stage()) {
		case GuardSetupStage::SUCCEEDED:
			return tr("Steam Guard of %1 is set up. Its codes are in the account list, and you'll get a notification about new confirmations.").arg(accountName);
		case GuardSetupStage::IDLE:
		case GuardSetupStage::ADDING:
			return tr("SteamForge becomes the Steam Guard mobile authenticator of %1: the codes for signing in and the confirmations of trades will be here. Steam sends a confirmation code by SMS or to the email of the account.").arg(accountName);
		default:
			return tr("Setting up Steam Guard of %1. Enter the confirmation code Steam sent.").arg(accountName);
	}
}

QString GuardSetupDialog::CodePrompt() const {
	const std::optional<PendingActivation>& pendingActivation = setup.Record().pendingActivation;
	if (!pendingActivation.has_value()) {
		return {};
	}
	if (pendingActivation->confirmType == EMAIL_ACTIVATION_CONFIRM_TYPE) {
		return tr("Steam sent an email with a confirmation code to the address of the account. Enter the code.");
	}
	if (!pendingActivation->phoneNumberHint.isEmpty()) {
		return tr("Steam sent an SMS with a confirmation code to the phone number of the account (%1). Enter the code.").arg(pendingActivation->phoneNumberHint);
	}
	return tr("Steam sent an SMS with a confirmation code to the phone number of the account. Enter the code.");
}

QString GuardSetupDialog::StatusText() const {
	switch (setup.Stage()) {
		case GuardSetupStage::ADDING:
			return tr("Asking Steam to create the authenticator…");
		case GuardSetupStage::FINALIZING:
			return tr("Confirming with Steam…");
		case GuardSetupStage::AWAITING_ACTIVATION_CODE:
		case GuardSetupStage::FAILED:
			return setup.Failure().has_value() ? FailureText(*setup.Failure()) : QString();
		default:
			return {};
	}
}

QString GuardSetupDialog::FailureText(const GuardSetupFailure& setupFailure) {
	switch (setupFailure.kind) {
		case GuardSetupFailureKind::SIGN_IN_REQUIRED:
			return tr("Steam no longer accepts the saved sign-in of this account. Sign in to Steam again and continue.");
		case GuardSetupFailureKind::ALREADY_PROTECTED:
			return tr("The account already has a Steam Guard mobile authenticator on another device. To use it in SteamForge, move it here with \"Move from a phone\".");
		case GuardSetupFailureKind::WRONG_ACTIVATION_CODE:
			return tr("Steam did not accept the confirmation code. Check it and enter it again.");
		case GuardSetupFailureKind::TIME_MISMATCH:
			return tr("Steam did not accept the Steam Guard code: the clock of this computer differs from Steam time. Wait until the time is checked with Steam and try again.");
		case GuardSetupFailureKind::NOT_ACCEPTED:
			return tr("Steam did not confirm the setup (status %1). Enter the code again.").arg(setupFailure.status);
		case GuardSetupFailureKind::CONNECTION:
			return tr("Steam could not be reached. If the connection broke while confirming, enter the same code again. The recovery code is already saved in the record.");
		case GuardSetupFailureKind::STORAGE_ERROR:
			return tr("The data could not be saved on this computer, so the setup stopped. Steam Guard was not turned on.");
		case GuardSetupFailureKind::ACTIVATED_NOT_SAVED:
			return tr("Steam turned Steam Guard on, but SteamForge could not update the record on this computer. Write down the recovery code below now.");
		case GuardSetupFailureKind::STEAM_ERROR:
			return DescribeWebApiError(tr("Steam Guard could not be set up."), setupFailure.error);
	}
	return {};
}

} // namespace SteamForge
