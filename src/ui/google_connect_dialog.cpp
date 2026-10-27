#include "ui/google_connect_dialog.h"

#include "app/account_library.h"
#include "app/cloud_sync.h"
#include "storage/private_file.h"
#include "ui/illustration_view.h"
#include "ui/theme.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int MINIMUM_DIALOG_WIDTH = 540;
	constexpr int DIALOG_SPACING = 12;
	constexpr int KEY_PIXEL_SIZE = 22;
	constexpr int KEY_INPUT_PIXEL_SIZE = 15;
	// Tighter than the shown key, so the whole key fits into the field.
	constexpr qreal KEY_INPUT_LETTER_SPACING = 1.0;
	// Four groups per line keep the key readable when it is copied onto paper.
	constexpr qsizetype KEY_GROUPS_PER_LINE = 4;
	constexpr QChar KEY_GROUP_SEPARATOR{u'-'};
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins CARD_MARGINS{16, 12, 16, 12};
	constexpr QSize ILLUSTRATION_SIZE{360, 190};
	constexpr QLatin1StringView SYNC_ILLUSTRATION_RESOURCE{":/illustrations/google_sync.svg"};
	constexpr QLatin1StringView KEY_ILLUSTRATION_RESOURCE{":/illustrations/recovery_key.svg"};
	constexpr QLatin1StringView DONE_ILLUSTRATION_RESOURCE{":/illustrations/all_set.svg"};
	constexpr QLatin1StringView KEY_PLACEHOLDER{"XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX-XXXX"};
	constexpr QLatin1StringView KEY_FILE_NAME{"SteamForge recovery key.txt"};
	constexpr std::chrono::milliseconds COPIED_FEEDBACK_DURATION{std::chrono::seconds(2)};
	constexpr int ACCOUNTS_LIST_MAXIMUM_HEIGHT = 180;
	constexpr int ACCOUNTS_LIST_SPACING = 6;

	QString KeyInTwoLines(const QString& keyText) {
		const QStringList groups = keyText.split(KEY_GROUP_SEPARATOR);
		if (groups.size() <= KEY_GROUPS_PER_LINE) {
			return keyText;
		}
		return groups.mid(0, KEY_GROUPS_PER_LINE).join(KEY_GROUP_SEPARATOR) + QLatin1Char('\n') + groups.mid(KEY_GROUPS_PER_LINE).join(KEY_GROUP_SEPARATOR);
	}

	QString Bullets(const QStringList& lines) {
		QStringList bulletLines;
		for (const QString& line : lines) {
			bulletLines.append(QStringLiteral("• ") + line);
		}
		return bulletLines.join(QLatin1Char('\n'));
	}
} // namespace

GoogleConnectDialog::GoogleConnectDialog(GoogleConnection& connection, CloudSync& sync, const AccountLibrary& library, AccountTitleProvider titleProvider, EncryptionStarter turnOnEncryption, bool isEncryptionOn, QWidget* parent) :
	QDialog(parent),
	googleConnection(connection),
	cloudSync(sync),
	accountLibrary(library),
	accountTitleProvider(std::move(titleProvider)),
	encryptionStarter(std::move(turnOnEncryption)),
	isEncrypted(isEncryptionOn) {
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);

	illustration = new IllustrationView(SYNC_ILLUSTRATION_RESOURCE, ILLUSTRATION_SIZE, this);
	headlineLabel = new QLabel(this);
	headlineLabel->setObjectName(PAGE_TITLE_STYLE);
	headlineLabel->setAlignment(Qt::AlignCenter);
	headlineLabel->setWordWrap(true);
	textLabel = new QLabel(this);
	textLabel->setWordWrap(true);
	noticeLabel = new QLabel(this);
	noticeLabel->setObjectName(NOTICE_STYLE);
	noticeLabel->setWordWrap(true);

	keyCard = new QFrame(this);
	keyCard->setObjectName(CARD_STYLE);
	keyTitleLabel = new QLabel(keyCard);
	MakeSectionLabel(*keyTitleLabel);
	keyLabel = new QLabel(keyCard);
	keyLabel->setFont(GuardCodeFont(KEY_PIXEL_SIZE));
	keyLabel->setAlignment(Qt::AlignCenter);
	keyLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	copyKeyButton = new QPushButton(keyCard);
	saveKeyButton = new QPushButton(keyCard);
	auto* keyButtonsLayout = new QHBoxLayout();
	keyButtonsLayout->addWidget(copyKeyButton);
	keyButtonsLayout->addWidget(saveKeyButton);
	keyButtonsLayout->addStretch();
	auto* keyCardLayout = new QVBoxLayout(keyCard);
	keyCardLayout->setContentsMargins(CARD_MARGINS);
	keyCardLayout->addWidget(keyTitleLabel);
	keyCardLayout->addWidget(keyLabel);
	keyCardLayout->addLayout(keyButtonsLayout);
	keySavedCheckBox = new QCheckBox(this);

	keyEdit = new QLineEdit(this);
	QFont keyInputFont = GuardCodeFont(KEY_INPUT_PIXEL_SIZE);
	keyInputFont.setLetterSpacing(QFont::AbsoluteSpacing, KEY_INPUT_LETTER_SPACING);
	keyEdit->setFont(keyInputFont);
	keyEdit->setPlaceholderText(KEY_PLACEHOLDER);
	replaceVaultButton = new QPushButton(this);
	replaceVaultButton->setObjectName(LINK_BUTTON_STYLE);
	replaceVaultButton->setCursor(Qt::PointingHandCursor);

	accountsList = new QWidget(this);
	auto* accountsListLayout = new QVBoxLayout(accountsList);
	accountsListLayout->setSpacing(ACCOUNTS_LIST_SPACING);
	accountsArea = new QScrollArea(this);
	accountsArea->setObjectName(DETAILS_SCROLL_STYLE);
	accountsArea->setFrameShape(QFrame::NoFrame);
	accountsArea->setWidgetResizable(true);
	accountsArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	accountsArea->setMaximumHeight(ACCOUNTS_LIST_MAXIMUM_HEIGHT);
	accountsArea->setWidget(accountsList);

	statusLabel = new QLabel(this);
	statusLabel->setObjectName(ERROR_TEXT_STYLE);
	statusLabel->setWordWrap(true);
	statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	primaryButton = new QPushButton(this);
	SetButtonRole(*primaryButton, ButtonRole::PRIMARY);
	secondaryButton = new QPushButton(this);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(primaryButton);
	buttonsLayout->addWidget(secondaryButton);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(illustration, 0, Qt::AlignHCenter);
	dialogLayout->addWidget(headlineLabel);
	dialogLayout->addWidget(textLabel);
	dialogLayout->addWidget(noticeLabel);
	dialogLayout->addWidget(keyCard);
	dialogLayout->addWidget(keySavedCheckBox);
	dialogLayout->addWidget(keyEdit);
	dialogLayout->addWidget(replaceVaultButton, 0, Qt::AlignLeft);
	dialogLayout->addWidget(accountsArea);
	dialogLayout->addWidget(statusLabel);
	dialogLayout->addLayout(buttonsLayout);

	connect(primaryButton, &QPushButton::clicked, this, &GoogleConnectDialog::PressPrimary);
	connect(secondaryButton, &QPushButton::clicked, this, [this]() {
		// After the connection the account choice is optional: skipping it keeps Google connected.
		if (step == Step::CHOOSE_ACCOUNTS) {
			ShowStep(Step::DONE);
			return;
		}
		reject();
	});
	connect(replaceVaultButton, &QPushButton::clicked, this, &GoogleConnectDialog::ConfirmVaultReplacement);
	connect(&cloudSync, &CloudSync::AddingFinished, this, &GoogleConnectDialog::CountUpload);
	connect(copyKeyButton, &QPushButton::clicked, this, &GoogleConnectDialog::CopyRecoveryKey);
	connect(saveKeyButton, &QPushButton::clicked, this, &GoogleConnectDialog::SaveRecoveryKeyToFile);
	connect(keySavedCheckBox, &QCheckBox::toggled, primaryButton, &QPushButton::setEnabled);
	connect(keyEdit, &QLineEdit::returnPressed, this, &GoogleConnectDialog::SubmitRecoveryKey);
	connect(&connection, &GoogleConnection::SignInPageReady, this, [this](const QUrl& pageUrl) {
		signInPageUrl = pageUrl;
		ShowStep(Step::BROWSER);
		OpenSignInPage();
	});
	connect(&connection, &GoogleConnection::NewVaultCreated, this, [this](const QString& keyText) {
		recoveryKeyText = keyText;
		isNewVault = true;
		ShowStep(Step::NEW_KEY);
	});
	connect(&connection, &GoogleConnection::RecoveryKeyNeeded, this, [this]() {
		ShowStep(Step::ENTER_KEY);
	});
	connect(&connection, &GoogleConnection::RecoveryKeyRejected, this, [this]() {
		statusLabel->setText(tr("This key does not open the cloud copy of this Google account."));
		statusLabel->show();
	});
	connect(&connection, &GoogleConnection::Connected, this, [this]() {
		FillAccountChoices();
		ShowStep(accountChoices.empty() ? Step::DONE : Step::CHOOSE_ACCOUNTS);
	});
	connect(&connection, &GoogleConnection::ConnectFailed, this, &GoogleConnectDialog::ShowFailure);

	ShowStep(Step::INTRODUCTION);
}

void GoogleConnectDialog::reject() {
	if (step == Step::UPLOADING) {
		return;
	}
	googleConnection.CancelConnecting();
	QDialog::reject();
}

void GoogleConnectDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void GoogleConnectDialog::RetranslateUi() {
	setWindowTitle(tr("Connect Google"));
	keyTitleLabel->setText(tr("Recovery key"));
	copyKeyButton->setText(tr("Copy"));
	saveKeyButton->setText(tr("Save to file…"));
	keySavedCheckBox->setText(tr("I saved the key in a safe place"));
	replaceVaultButton->setText(tr("No key? Make a new cloud copy…"));
	secondaryButton->setText(step == Step::CHOOSE_ACCOUNTS ? tr("Not now") : tr("Cancel"));
	switch (step) {
		case Step::INTRODUCTION:
			headlineLabel->setText(tr("Sync through Google"));
			textLabel->setText(Bullets({
				tr("SteamForge keeps a copy of the accounts you choose in its hidden folder on your Google Drive. The folder does not show up in Drive, and other programs cannot open it."),
				tr("The accounts are encrypted before they leave this computer. The key stays with you, so Google cannot read them."),
				tr("Nothing is sent on its own: you add accounts to Google one by one."),
				tr("To open the copy on another computer, you need the recovery key that SteamForge shows in a moment."),
			}));
			noticeLabel->setText(tr("Encryption on this computer turns on first: the key of the cloud copy is kept under it."));
			primaryButton->setText(isEncrypted ? tr("Sign in with Google") : tr("Turn on encryption and sign in"));
			break;
		case Step::BROWSER:
			headlineLabel->setText(tr("Continue in the browser"));
			textLabel->setText(tr("Sign in to your Google account in the browser and allow SteamForge access. Keep the Google Drive permission ticked: without it SteamForge has nowhere to keep the copy."));
			primaryButton->setText(tr("Open the page again"));
			break;
		case Step::NEW_KEY:
			headlineLabel->setText(isReplacingVault ? tr("Save the new recovery key") : tr("Save the recovery key"));
			textLabel->setText(tr("It opens the cloud copy on another computer. Without the key and without this computer nobody can open the copy, not even you."));
			noticeLabel->setText(isReplacingVault ? tr("The old cloud copy is deleted when you press Done. The accounts on this computer stay. Keep the new key apart from this computer.") : tr("Keep it apart from this computer: on paper, in a password manager or on another device."));
			primaryButton->setText(tr("Done"));
			break;
		case Step::ENTER_KEY:
			headlineLabel->setText(tr("Enter the recovery key"));
			textLabel->setText(tr("Your Google account already has a SteamForge cloud copy. Enter the recovery key that SteamForge showed when the copy was made."));
			primaryButton->setText(tr("Open"));
			break;
		case Step::SAVING:
			headlineLabel->setText(tr("Saving the cloud copy…"));
			textLabel->setText(isReplacingVault ? tr("SteamForge is deleting the old copy and creating a new encrypted one in your Google account.") : tr("SteamForge is creating the encrypted copy in your Google account."));
			primaryButton->setText(tr("Done"));
			break;
		case Step::CHOOSE_ACCOUNTS:
			headlineLabel->setText(tr("Add accounts to Google"));
			textLabel->setText(isNewVault ? tr("The ticked accounts go into the cloud copy. The others stay only on this computer.") : tr("The accounts from Google come to this computer by themselves. The ticked accounts of this computer go into Google too; the others stay only here."));
			primaryButton->setText(tr("Add to Google"));
			break;
		case Step::UPLOADING:
			headlineLabel->setText(tr("Adding accounts…"));
			primaryButton->setText(tr("Add to Google"));
			break;
		case Step::DONE:
			headlineLabel->setText(tr("Google is connected"));
			textLabel->setText(DoneText());
			primaryButton->setText(tr("Done"));
			break;
	}
}

void GoogleConnectDialog::ShowStep(Step newStep) {
	step = newStep;
	const bool isKeyStep = step == Step::NEW_KEY || step == Step::ENTER_KEY;
	if (step == Step::DONE) {
		illustration->SetResource(DONE_ILLUSTRATION_RESOURCE);
	} else {
		illustration->SetResource(isKeyStep ? KEY_ILLUSTRATION_RESOURCE : SYNC_ILLUSTRATION_RESOURCE);
	}
	noticeLabel->setVisible((step == Step::INTRODUCTION && !isEncrypted) || step == Step::NEW_KEY);
	keyCard->setVisible(step == Step::NEW_KEY);
	keyLabel->setText(KeyInTwoLines(recoveryKeyText));
	keySavedCheckBox->setVisible(step == Step::NEW_KEY);
	keySavedCheckBox->setChecked(false);
	keyEdit->setVisible(step == Step::ENTER_KEY);
	replaceVaultButton->setVisible(step == Step::ENTER_KEY);
	accountsArea->setVisible(step == Step::CHOOSE_ACCOUNTS || step == Step::UPLOADING);
	accountsList->setEnabled(step == Step::CHOOSE_ACCOUNTS);
	if (step == Step::ENTER_KEY) {
		keyEdit->clear();
		keyEdit->setFocus();
	}
	if (step != Step::INTRODUCTION) {
		statusLabel->clear();
	}
	statusLabel->setVisible(!statusLabel->text().isEmpty());
	primaryButton->setEnabled(step != Step::NEW_KEY && step != Step::SAVING && step != Step::UPLOADING);
	secondaryButton->setVisible(step != Step::DONE && step != Step::UPLOADING);
	secondaryButton->setEnabled(step != Step::SAVING);
	RetranslateUi();
	if (step == Step::CHOOSE_ACCOUNTS) {
		UpdateUploadButton();
	}
	FitHeightToContents(*this);
}

void GoogleConnectDialog::PressPrimary() {
	switch (step) {
		case Step::INTRODUCTION:
			StartSignIn();
			return;
		case Step::BROWSER:
			OpenSignInPage();
			return;
		case Step::NEW_KEY:
			if (keySavedCheckBox->isChecked()) {
				ShowStep(Step::SAVING);
				googleConnection.ConfirmRecoveryKeySaved();
			}
			return;
		case Step::ENTER_KEY:
			SubmitRecoveryKey();
			return;
		case Step::SAVING:
		case Step::UPLOADING:
			return;
		case Step::CHOOSE_ACCOUNTS:
			StartUpload();
			return;
		case Step::DONE:
			accept();
			return;
	}
}

void GoogleConnectDialog::ConfirmVaultReplacement() {
	QMessageBox confirmation(QMessageBox::Warning, tr("New cloud copy"), tr("Delete the old cloud copy and make a new one?"), QMessageBox::NoButton, this);
	confirmation.setInformativeText(tr("The old copy in your Google account is deleted for good, and nobody can open it again. The accounts on this computer stay. Other computers that used the old copy have to be connected again with the new key."));
	const QPushButton* replaceButton = confirmation.addButton(tr("Delete and make a new one"), QMessageBox::DestructiveRole);
	confirmation.setDefaultButton(confirmation.addButton(QMessageBox::Cancel));
	confirmation.exec();
	if (confirmation.clickedButton() != replaceButton) {
		return;
	}
	isReplacingVault = true;
	statusLabel->clear();
	googleConnection.ReplaceVault();
}

void GoogleConnectDialog::FillAccountChoices() {
	for (const auto& [recordId, checkBox] : accountChoices) {
		delete checkBox;
	}
	accountChoices.clear();
	for (const AuthenticatorRecord& record : accountLibrary.Records()) {
		// Only an account that makes codes is worth keeping in the cloud.
		if (!DetermineCapabilities(record).canGenerateCodes || cloudSync.IsInCloud(record.id)) {
			continue;
		}
		auto* checkBox = new QCheckBox(accountTitleProvider(record), accountsList);
		checkBox->setChecked(true);
		connect(checkBox, &QCheckBox::toggled, this, &GoogleConnectDialog::UpdateUploadButton);
		accountsList->layout()->addWidget(checkBox);
		accountChoices.emplace_back(record.id, checkBox);
	}
}

void GoogleConnectDialog::StartUpload() {
	std::vector<QUuid> chosenRecordIds;
	for (const auto& [recordId, checkBox] : accountChoices) {
		if (checkBox->isChecked() && accountLibrary.FindRecord(recordId) != nullptr) {
			chosenRecordIds.push_back(recordId);
		}
	}
	if (chosenRecordIds.empty()) {
		return;
	}
	uploadingRecordIds = QSet<QUuid>(chosenRecordIds.begin(), chosenRecordIds.end());
	uploadTotalCount = static_cast<int>(chosenRecordIds.size());
	ShowStep(Step::UPLOADING);
	textLabel->setText(tr("Done %1 of %2.").arg(0).arg(uploadTotalCount));
	// An account that is already in Google reports at once, inside the loop.
	for (const QUuid& recordId : chosenRecordIds) {
		cloudSync.AddToCloud(recordId);
	}
}

void GoogleConnectDialog::CountUpload(const QUuid& recordId, bool isAdded) {
	if (!uploadingRecordIds.remove(recordId)) {
		return;
	}
	if (isAdded) {
		++uploadedCount;
	} else if (cloudSync.HasConflict(recordId)) {
		++conflictUploadCount;
	} else {
		++failedUploadCount;
	}
	if (uploadingRecordIds.isEmpty()) {
		ShowStep(Step::DONE);
		return;
	}
	textLabel->setText(tr("Done %1 of %2.").arg(uploadTotalCount - static_cast<int>(uploadingRecordIds.size())).arg(uploadTotalCount));
}

void GoogleConnectDialog::UpdateUploadButton() {
	primaryButton->setEnabled(std::ranges::any_of(accountChoices, [](const auto& choice) {
		return choice.second->isChecked();
	}));
}

QString GoogleConnectDialog::DoneText() const {
	const QString& email = googleConnection.Email();
	QStringList lines{email.isEmpty() ? tr("SteamForge is connected to your Google account.") : tr("SteamForge is connected to the Google account %1.").arg(email)};
	if (uploadTotalCount > 0) {
		lines.append(tr("The cloud copy is encrypted, and this computer keeps its key."));
		lines.append(tr("Accounts added to Google: %1.").arg(uploadedCount));
		if (conflictUploadCount > 0) {
			lines.append(tr("Google already has another Steam Guard for %1 of them: the panel of the account asks which one to keep.").arg(conflictUploadCount));
		}
		if (failedUploadCount > 0) {
			lines.append(tr("Accounts that could not be added: %1. They stay on this computer.").arg(failedUploadCount));
		}
	} else if (!isNewVault) {
		lines.append(tr("The accounts from Google come to this computer by themselves; the accounts of this computer go to Google one by one, as you add them."));
	} else {
		lines.append(tr("The cloud copy is encrypted, and this computer keeps its key. Accounts go to Google one by one: nothing is sent until you add it."));
	}
	return lines.join(QLatin1Char(' '));
}

void GoogleConnectDialog::StartSignIn() {
	if (!isEncrypted) {
		if (!encryptionStarter || !encryptionStarter()) {
			return;
		}
		isEncrypted = true;
	}
	statusLabel->clear();
	statusLabel->hide();
	googleConnection.Connect();
}

void GoogleConnectDialog::OpenSignInPage() {
	if (!QDesktopServices::openUrl(signInPageUrl)) {
		statusLabel->setText(tr("The browser did not open. Copy this address into a browser: %1").arg(signInPageUrl.toString()));
		statusLabel->show();
		FitHeightToContents(*this);
	}
}

void GoogleConnectDialog::SubmitRecoveryKey() {
	const std::optional<RecoveryKey> recoveryKey = RecoveryKey::Parse(keyEdit->text());
	if (!recoveryKey.has_value()) {
		statusLabel->setText(tr("The key has 32 letters and digits in eight groups of four."));
		statusLabel->show();
		FitHeightToContents(*this);
		return;
	}
	statusLabel->clear();
	statusLabel->hide();
	googleConnection.OpenVault(*recoveryKey);
}

void GoogleConnectDialog::CopyRecoveryKey() {
	QGuiApplication::clipboard()->setText(recoveryKeyText);
	copyKeyButton->setText(tr("Copied"));
	QTimer::singleShot(COPIED_FEEDBACK_DURATION, copyKeyButton, [this]() {
		copyKeyButton->setText(tr("Copy"));
	});
}

void GoogleConnectDialog::SaveRecoveryKeyToFile() {
	const QString filePath = QFileDialog::getSaveFileName(this, tr("Save the recovery key"), QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(KEY_FILE_NAME), tr("Text files (*.txt)"));
	if (filePath.isEmpty()) {
		return;
	}
	const QString contents = tr("SteamForge recovery key") + QStringLiteral("\n\n") + recoveryKeyText + QStringLiteral("\n\n") + tr("It opens the SteamForge cloud copy of your Google account on another computer. Keep this file private.") + QLatin1Char('\n');
	if (!WriteOwnerOnlyFile(filePath, contents.toUtf8()).has_value()) {
		QMessageBox::warning(this, windowTitle(), tr("The key could not be saved to this file."));
	}
}

void GoogleConnectDialog::ShowFailure(GoogleConnectFailure failure) {
	statusLabel->setText(FailureText(failure));
	ShowStep(Step::INTRODUCTION);
}

QString GoogleConnectDialog::FailureText(GoogleConnectFailure failure) {
	switch (failure) {
		case GoogleConnectFailure::ENCRYPTION_OFF:
			return tr("Turn on encryption on this computer first.");
		case GoogleConnectFailure::RECEIVER_UNAVAILABLE:
			return tr("SteamForge could not receive the answer of Google on this computer. Try again.");
		case GoogleConnectFailure::SIGN_IN_CANCELLED:
			return tr("Signing in to Google was cancelled.");
		case GoogleConnectFailure::DRIVE_PERMISSION_MISSING:
			return tr("Google did not allow access to the SteamForge folder on Drive. Sign in again and keep the Google Drive permission ticked.");
		case GoogleConnectFailure::GOOGLE_UNREACHABLE:
			return tr("Google could not be reached. Check the internet connection and try again.");
		case GoogleConnectFailure::OTHER_VAULT:
			return tr("This Google account does not have the cloud copy this computer uses. Sign in to the Google account you connected before.");
		case GoogleConnectFailure::VAULT_NEWER_VERSION:
			return tr("A newer SteamForge made this cloud copy. Update SteamForge to open it.");
		case GoogleConnectFailure::VAULT_DAMAGED:
			return tr("The cloud copy in Google is damaged and does not open.");
		case GoogleConnectFailure::KEY_GENERATION_FAILED:
			return tr("The system could not make a key. Try again.");
		case GoogleConnectFailure::SAVE_FAILED:
			return tr("The connection could not be saved on this computer.");
	}
	return {};
}

} // namespace SteamForge
