#include "ui/settings_dialog.h"

#include "app/google_connection.h"
#include "common/version.h"
#include "ui/image_resources.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	// Language names are shown in their own language, so they are not translated.
	constexpr QLatin1StringView ENGLISH_LANGUAGE_NAME{"English"};
	constexpr QStringView RUSSIAN_LANGUAGE_NAME{u"Русский"};
	constexpr int SYSTEM_LANGUAGE_INDEX = 0;
	constexpr int ENGLISH_LANGUAGE_INDEX = 1;
	constexpr int RUSSIAN_LANGUAGE_INDEX = 2;
	constexpr QSize MINIMUM_DIALOG_SIZE{720, 500};
	constexpr int NAVIGATION_WIDTH = 190;
	constexpr int PAGE_SPACING = 12;
	constexpr int SECTION_SPACING = 20;
	constexpr int STATUS_SPACING = 14;
	constexpr int STATUS_TEXT_SPACING = 2;
	constexpr QSize STATUS_ICON_SIZE{36, 36};
	constexpr QMargins PAGE_MARGINS{24, 20, 24, 12};
	constexpr QMargins BUTTONS_MARGINS{24, 0, 24, 20};

	int LanguageIndex(std::optional<InterfaceLanguage> language) {
		if (!language.has_value()) {
			return SYSTEM_LANGUAGE_INDEX;
		}
		switch (*language) {
			case InterfaceLanguage::ENGLISH:
				return ENGLISH_LANGUAGE_INDEX;
			case InterfaceLanguage::RUSSIAN:
				return RUSSIAN_LANGUAGE_INDEX;
		}
		return SYSTEM_LANGUAGE_INDEX;
	}

	std::optional<InterfaceLanguage> LanguageAtIndex(int comboIndex) {
		switch (comboIndex) {
			case ENGLISH_LANGUAGE_INDEX:
				return InterfaceLanguage::ENGLISH;
			case RUSSIAN_LANGUAGE_INDEX:
				return InterfaceLanguage::RUSSIAN;
			default:
				return std::nullopt;
		}
	}

	QVBoxLayout* PageLayout(QWidget& page) {
		auto* pageLayout = new QVBoxLayout(&page);
		pageLayout->setContentsMargins(PAGE_MARGINS);
		pageLayout->setSpacing(PAGE_SPACING);
		return pageLayout;
	}

	QLabel* CreatePageTitle(QWidget* page) {
		auto* titleLabel = new QLabel(page);
		titleLabel->setObjectName(PAGE_TITLE_STYLE);
		return titleLabel;
	}

	// An icon beside a headline and, for some states, a line under it, as a card of the account panel shows its state.
	QHBoxLayout* StatusRow(QLabel& iconLabel, QLabel& headlineLabel, QLabel* detailLabel = nullptr) {
		iconLabel.setFixedSize(STATUS_ICON_SIZE);
		headlineLabel.setObjectName(STATE_HEADLINE_STYLE);
		headlineLabel.setWordWrap(true);
		auto* textLayout = new QVBoxLayout();
		textLayout->setSpacing(STATUS_TEXT_SPACING);
		textLayout->addWidget(&headlineLabel);
		if (detailLabel != nullptr) {
			detailLabel->setObjectName(DETAIL_TEXT_STYLE);
			textLayout->addWidget(detailLabel);
		}
		auto* statusLayout = new QHBoxLayout();
		statusLayout->setSpacing(STATUS_SPACING);
		statusLayout->addWidget(&iconLabel, 0, Qt::AlignVCenter);
		statusLayout->addLayout(textLayout, 1);
		return statusLayout;
	}

	void SetStatusIcon(QLabel& iconLabel, QLatin1StringView resource) {
		iconLabel.setPixmap(QIcon(resource).pixmap(STATUS_ICON_SIZE, iconLabel.devicePixelRatioF()));
	}
} // namespace

SettingsDialog::SettingsDialog(std::optional<InterfaceLanguage> currentLanguage, bool checksForUpdates, DataLocation location, const StorageEncryption& encryption, GoogleConnection& google, const CloudAccountsSource& cloudAccounts, SettingsSection section, QWidget* parent) :
	QDialog(parent),
	dataLocation(std::move(location)),
	storageEncryption(encryption),
	googleConnection(google) {
	setMinimumSize(MINIMUM_DIALOG_SIZE);

	navigationList = new QListWidget(this);
	navigationList->setObjectName(SETTINGS_NAVIGATION_STYLE);
	navigationList->setFixedWidth(NAVIGATION_WIDTH);
	navigationList->setFrameShape(QFrame::NoFrame);
	navigationList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	pageStack = new QStackedWidget(this);
	for (QWidget* page : {CreateGeneralPage(), CreateEncryptionPage(), CreateGooglePage(cloudAccounts), CreateUpdatesPage()}) {
		pageStack->addWidget(page);
		navigationList->addItem(QString());
	}
	languageComboBox->setCurrentIndex(LanguageIndex(currentLanguage));
	updateChecksCheckBox->setChecked(checksForUpdates);

	buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);
	auto* contentLayout = new QVBoxLayout();
	contentLayout->setContentsMargins(0, 0, 0, 0);
	contentLayout->addWidget(pageStack, 1);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->setContentsMargins(BUTTONS_MARGINS);
	// Rare and lasting, so it waits at the bottom of the Google section, apart from its accounts.
	buttonsLayout->addWidget(googleDisconnectButton);
	buttonsLayout->addWidget(buttonBox, 1);
	contentLayout->addLayout(buttonsLayout);
	auto* dialogLayout = new QHBoxLayout(this);
	dialogLayout->setContentsMargins(0, 0, 0, 0);
	dialogLayout->setSpacing(0);
	dialogLayout->addWidget(navigationList);
	dialogLayout->addLayout(contentLayout, 1);

	connect(navigationList, &QListWidget::currentRowChanged, pageStack, &QStackedWidget::setCurrentIndex);
	connect(pageStack, &QStackedWidget::currentChanged, this, &SettingsDialog::UpdateGooglePage);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(&encryption, &StorageEncryption::StatusChanged, this, &SettingsDialog::UpdateEncryptionPage);
	connect(&google, &GoogleConnection::StateChanged, this, &SettingsDialog::UpdateGooglePage);
	connect(&google, &GoogleConnection::StateChanged, this, &SettingsDialog::UpdateEncryptionPage);

	RetranslateUi();
	ShowSection(section);
}

void SettingsDialog::ShowSection(SettingsSection section) {
	navigationList->setCurrentRow(static_cast<int>(section));
}

void SettingsDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

QWidget* SettingsDialog::CreateGeneralPage() {
	auto* page = new QWidget(this);
	generalTitleLabel = CreatePageTitle(page);
	languageSectionLabel = new QLabel(page);
	MakeSectionLabel(*languageSectionLabel);
	languageComboBox = new QComboBox(page);
	languageComboBox->addItem(QString());
	languageComboBox->addItem(ENGLISH_LANGUAGE_NAME);
	languageComboBox->addItem(RUSSIAN_LANGUAGE_NAME.toString());

	QVBoxLayout* pageLayout = PageLayout(*page);
	pageLayout->addWidget(generalTitleLabel);
	pageLayout->addWidget(languageSectionLabel);
	pageLayout->addWidget(languageComboBox);
	pageLayout->addStretch();

	connect(languageComboBox, &QComboBox::activated, this, &SettingsDialog::SelectLanguage);
	return page;
}

QWidget* SettingsDialog::CreateEncryptionPage() {
	auto* page = new QWidget(this);
	encryptionTitleLabel = CreatePageTitle(page);
	encryptionIconLabel = new QLabel(page);
	encryptionHeadlineLabel = new QLabel(page);
	encryptionTextLabel = new QLabel(page);
	encryptionTextLabel->setObjectName(DETAIL_TEXT_STYLE);
	encryptionTextLabel->setWordWrap(true);
	encryptionGoogleLabel = new QLabel(page);
	encryptionGoogleLabel->setObjectName(NOTICE_STYLE);
	encryptionGoogleLabel->setWordWrap(true);
	encryptionActionButton = new QPushButton(page);

	dataFolderSectionLabel = new QLabel(page);
	MakeSectionLabel(*dataFolderSectionLabel);
	// A field keeps a long path whole and copyable instead of breaking it inside a word.
	dataFolderEdit = new QLineEdit(QDir::toNativeSeparators(dataLocation.directoryPath), page);
	dataFolderEdit->setReadOnly(true);
	dataFolderEdit->setCursorPosition(0);
	openFolderButton = new QPushButton(page);
	portableModeLabel = new QLabel(page);
	portableModeLabel->setObjectName(SECONDARY_TEXT_STYLE);
	portableModeLabel->setWordWrap(true);

	auto* actionLayout = new QHBoxLayout();
	actionLayout->addWidget(encryptionActionButton);
	actionLayout->addStretch();
	auto* folderRowLayout = new QHBoxLayout();
	folderRowLayout->addWidget(dataFolderEdit, 1);
	folderRowLayout->addWidget(openFolderButton);

	QVBoxLayout* pageLayout = PageLayout(*page);
	pageLayout->addWidget(encryptionTitleLabel);
	pageLayout->addLayout(StatusRow(*encryptionIconLabel, *encryptionHeadlineLabel));
	pageLayout->addWidget(encryptionTextLabel);
	pageLayout->addWidget(encryptionGoogleLabel);
	pageLayout->addLayout(actionLayout);
	pageLayout->addSpacing(SECTION_SPACING);
	pageLayout->addWidget(dataFolderSectionLabel);
	pageLayout->addLayout(folderRowLayout);
	pageLayout->addWidget(portableModeLabel);
	pageLayout->addStretch();

	connect(encryptionActionButton, &QPushButton::clicked, this, &SettingsDialog::PressEncryptionAction);
	connect(openFolderButton, &QPushButton::clicked, this, &SettingsDialog::OpenDataFolder);
	return page;
}

QWidget* SettingsDialog::CreateGooglePage(const CloudAccountsSource& cloudAccounts) {
	auto* page = new QWidget(this);
	googleTitleLabel = CreatePageTitle(page);
	googleIconLabel = new QLabel(page);
	googleHeadlineLabel = new QLabel(page);
	googleEmailLabel = new QLabel(page);
	googleEmailLabel->setTextFormat(Qt::PlainText);
	googleTextLabel = new QLabel(page);
	googleTextLabel->setObjectName(DETAIL_TEXT_STYLE);
	googleTextLabel->setWordWrap(true);
	googleConnectButton = new QPushButton(page);
	googleDisconnectButton = new QPushButton(this);
	cloudAccountsView = new CloudAccountsView(cloudAccounts.sync, googleConnection, cloudAccounts.library, cloudAccounts.titleProvider, cloudAccounts.profiles, page);

	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addWidget(googleConnectButton);
	buttonsLayout->addStretch();

	QVBoxLayout* pageLayout = PageLayout(*page);
	pageLayout->addWidget(googleTitleLabel);
	pageLayout->addLayout(StatusRow(*googleIconLabel, *googleHeadlineLabel, googleEmailLabel));
	pageLayout->addWidget(googleTextLabel);
	pageLayout->addLayout(buttonsLayout);
	pageLayout->addSpacing(SECTION_SPACING);
	pageLayout->addWidget(cloudAccountsView);
	pageLayout->addStretch();

	connect(googleConnectButton, &QPushButton::clicked, this, &SettingsDialog::GoogleConnectRequested);
	connect(googleDisconnectButton, &QPushButton::clicked, this, &SettingsDialog::GoogleDisconnectRequested);
	return page;
}

QWidget* SettingsDialog::CreateUpdatesPage() {
	auto* page = new QWidget(this);
	updatesTitleLabel = CreatePageTitle(page);
	updateChecksCheckBox = new QCheckBox(page);
	updatesNoteLabel = new QLabel(page);
	updatesNoteLabel->setObjectName(SECONDARY_TEXT_STYLE);
	updatesNoteLabel->setWordWrap(true);

	QVBoxLayout* pageLayout = PageLayout(*page);
	pageLayout->addWidget(updatesTitleLabel);
	pageLayout->addWidget(updateChecksCheckBox);
	pageLayout->addWidget(updatesNoteLabel);
	pageLayout->addStretch();

	connect(updateChecksCheckBox, &QCheckBox::toggled, this, &SettingsDialog::UpdateChecksToggled);
	return page;
}

void SettingsDialog::RetranslateUi() {
	setWindowTitle(tr("Settings"));
	const std::array<QString, 4> sectionNames{tr("General"), tr("Encryption"), tr("Google"), tr("Updates")};
	for (int row = 0; row < navigationList->count(); ++row) {
		navigationList->item(row)->setText(sectionNames.at(static_cast<std::size_t>(row)));
	}
	generalTitleLabel->setText(tr("General"));
	languageSectionLabel->setText(tr("Language"));
	languageComboBox->setItemText(SYSTEM_LANGUAGE_INDEX, tr("System language"));
	encryptionTitleLabel->setText(tr("Encryption"));
	dataFolderSectionLabel->setText(tr("Data folder"));
	openFolderButton->setText(tr("Open folder"));
	if (dataLocation.isPortable) {
		portableModeLabel->setText(tr("Portable mode: the .portable file next to SteamForge keeps the data in this folder next to the program."));
	} else {
		portableModeLabel->setText(tr("To keep the data next to the program instead, place an empty file named .portable next to SteamForge and restart it. Existing data is not moved automatically."));
	}
	googleTitleLabel->setText(tr("Google"));
	googleDisconnectButton->setText(tr("Disconnect on this computer"));
	updatesTitleLabel->setText(tr("Updates"));
	updateChecksCheckBox->setText(tr("Check for new versions"));
	updatesNoteLabel->setText(tr("Once a day SteamForge asks GitHub which version is the latest and offers a new one in a window. Nothing is downloaded or installed by itself. You have version %1.").arg(APPLICATION_VERSION));
	buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
	UpdateEncryptionPage();
	UpdateGooglePage();
}

void SettingsDialog::UpdateEncryptionPage() {
	const bool isGoogleConnected = googleConnection.HasSavedConnection();
	encryptionGoogleLabel->setVisible(false);
	encryptionActionButton->setVisible(true);
	SetButtonRole(*encryptionActionButton, ButtonRole::SECONDARY);
	switch (storageEncryption.Status()) {
		case EncryptionStatus::OFF:
			SetStatusIcon(*encryptionIconLabel, LOCK_OPEN_ICON_RESOURCE);
			encryptionHeadlineLabel->setText(tr("Data on this computer is not encrypted"));
			encryptionTextLabel->setText(tr("Turned on, the accounts and Steam sign-ins on this computer are stored encrypted. It protects copies of the data folder, backups and folder sync; it does not protect from programs running under your user account."));
			encryptionActionButton->setText(tr("Turn on encryption"));
			SetButtonRole(*encryptionActionButton, ButtonRole::PRIMARY);
			break;
		case EncryptionStatus::ON:
			SetStatusIcon(*encryptionIconLabel, LOCK_ICON_RESOURCE);
			encryptionHeadlineLabel->setText(tr("Data on this computer is encrypted"));
			switch (storageEncryption.Protection()) {
				case KeyProtection::WINDOWS_HELLO:
					encryptionTextLabel->setText(tr("Windows Hello guards the key: SteamForge asks for your PIN, face or fingerprint at every start."));
					break;
				case KeyProtection::KEYRING_PASSWORD:
					encryptionTextLabel->setText(tr("The key is in the SteamForge keyring, whose password the system asks at every start."));
					break;
				case KeyProtection::USER_ACCOUNT:
#ifdef Q_OS_WIN
					encryptionTextLabel->setText(tr("Windows keeps the key for your user account, so nothing is asked at start."));
#else
					encryptionTextLabel->setText(tr("The keyring of the system keeps the key, so nothing is asked at start."));
#endif
					break;
			}
			// The key of the cloud copy is kept under the local key: turned off, it would lie on the disk in the clear.
			encryptionGoogleLabel->setText(tr("Google is connected: the key of the cloud copy is kept under this encryption, so it stays on. Disconnect Google first to turn it off."));
			encryptionGoogleLabel->setVisible(isGoogleConnected);
			encryptionActionButton->setText(tr("Turn off encryption…"));
			encryptionActionButton->setVisible(!isGoogleConnected);
			break;
		case EncryptionStatus::LOCKED:
			SetStatusIcon(*encryptionIconLabel, LOCK_ALERT_ICON_RESOURCE);
			encryptionHeadlineLabel->setText(tr("SteamForge is locked"));
			encryptionTextLabel->setText(tr("Unlock it to open the accounts."));
			encryptionActionButton->setText(tr("Unlock"));
			SetButtonRole(*encryptionActionButton, ButtonRole::PRIMARY);
			break;
		case EncryptionStatus::KEY_UNAVAILABLE:
			SetStatusIcon(*encryptionIconLabel, LOCK_ALERT_ICON_RESOURCE);
			encryptionHeadlineLabel->setText(tr("The encryption key is not available"));
			encryptionTextLabel->setText(tr("The encrypted files stay unchanged. They open again under the same user account on the computer where encryption was turned on, once the system key store is unlocked."));
			encryptionActionButton->setVisible(false);
			break;
		case EncryptionStatus::UNSUPPORTED:
			SetStatusIcon(*encryptionIconLabel, LOCK_OPEN_ICON_RESOURCE);
			encryptionHeadlineLabel->setText(tr("Encryption is not available"));
			encryptionTextLabel->setText(tr("The key is kept in the keyring of the system, such as GNOME Keyring or KWallet. Start one of them to turn on encryption."));
			encryptionActionButton->setVisible(false);
			break;
	}
}

void SettingsDialog::UpdateGooglePage() {
	const QString& email = googleConnection.Email();
	googleConnectButton->setText(tr("Connect Google…"));
	googleEmailLabel->clear();
	bool canConnect = false;
	bool canDisconnect = false;
	switch (googleConnection.State()) {
		case GoogleConnectionState::UNAVAILABLE:
			SetStatusIcon(*googleIconLabel, CLOUD_OFF_ICON_RESOURCE);
			googleHeadlineLabel->setText(tr("Google is not available"));
			googleTextLabel->setText(tr("This build of SteamForge has no Google connection."));
			break;
		case GoogleConnectionState::DISCONNECTED:
			SetStatusIcon(*googleIconLabel, CLOUD_OFF_ICON_RESOURCE);
			googleHeadlineLabel->setText(tr("Google is not connected"));
			googleTextLabel->setText(tr("Accounts you choose can be kept in your Google account and opened on other computers. SteamForge encrypts them before they leave this computer."));
			canConnect = true;
			break;
		case GoogleConnectionState::LOCKED:
			SetStatusIcon(*googleIconLabel, CLOUD_ICON_RESOURCE);
			googleHeadlineLabel->setText(tr("Google is connected"));
			googleTextLabel->setText(tr("Unlock SteamForge to use it."));
			break;
		case GoogleConnectionState::CONNECTING:
			SetStatusIcon(*googleIconLabel, CLOUD_ICON_RESOURCE);
			googleHeadlineLabel->setText(tr("Connecting…"));
			googleTextLabel->clear();
			break;
		case GoogleConnectionState::CONNECTED:
			SetStatusIcon(*googleIconLabel, CLOUD_CHECK_ICON_RESOURCE);
			googleHeadlineLabel->setText(tr("Account connected"));
			googleEmailLabel->setText(email);
			googleTextLabel->setText(tr("The copies are encrypted. The key is kept on this computer."));
			canDisconnect = true;
			break;
		case GoogleConnectionState::SIGN_IN_REQUIRED:
			SetStatusIcon(*googleIconLabel, CLOUD_ALERT_ICON_RESOURCE);
			googleHeadlineLabel->setText(tr("Sign in to Google again"));
			googleTextLabel->setText(tr("Google no longer accepts the saved sign-in. Sign in again; the recovery key is not needed."));
			googleConnectButton->setText(tr("Sign in again…"));
			canConnect = true;
			canDisconnect = true;
			break;
	}
	googleTextLabel->setVisible(!googleTextLabel->text().isEmpty());
	googleEmailLabel->setVisible(!googleEmailLabel->text().isEmpty());
	cloudAccountsView->setVisible(googleConnection.State() == GoogleConnectionState::CONNECTED);
	googleConnectButton->setVisible(canConnect);
	SetButtonRole(*googleConnectButton, canConnect ? ButtonRole::PRIMARY : ButtonRole::SECONDARY);
	googleDisconnectButton->setVisible(canDisconnect && pageStack->currentIndex() == static_cast<int>(SettingsSection::GOOGLE));
}

CloudAccountsView& SettingsDialog::CloudAccounts() {
	return *cloudAccountsView;
}

void SettingsDialog::SelectLanguage(int comboIndex) {
	Q_EMIT LanguageSelected(LanguageAtIndex(comboIndex));
}

void SettingsDialog::OpenDataFolder() {
	if (!QDir().mkpath(dataLocation.directoryPath) || !QDesktopServices::openUrl(QUrl::fromLocalFile(dataLocation.directoryPath))) {
		QMessageBox::warning(this, windowTitle(), tr("The data folder could not be opened."));
	}
}

void SettingsDialog::PressEncryptionAction() {
	switch (storageEncryption.Status()) {
		case EncryptionStatus::OFF:
			Q_EMIT EncryptionTurnOnRequested();
			return;
		case EncryptionStatus::ON:
			Q_EMIT EncryptionTurnOffRequested();
			return;
		case EncryptionStatus::LOCKED:
			Q_EMIT UnlockRequested();
			return;
		case EncryptionStatus::KEY_UNAVAILABLE:
		case EncryptionStatus::UNSUPPORTED:
			return;
	}
}

} // namespace SteamForge
