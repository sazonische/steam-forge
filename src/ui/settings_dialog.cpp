#include "ui/settings_dialog.h"

#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	// Language names are shown in their own language, so they are not translated.
	constexpr QLatin1StringView ENGLISH_LANGUAGE_NAME{"English"};
	constexpr QStringView RUSSIAN_LANGUAGE_NAME{u"Русский"};
	constexpr int SYSTEM_LANGUAGE_INDEX = 0;
	constexpr int ENGLISH_LANGUAGE_INDEX = 1;
	constexpr int RUSSIAN_LANGUAGE_INDEX = 2;
	constexpr int MINIMUM_DIALOG_WIDTH = 560;
	constexpr int DIALOG_SPACING = 12;
	constexpr int SECTION_SPACING = 20;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};

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
} // namespace

SettingsDialog::SettingsDialog(std::optional<InterfaceLanguage> currentLanguage, DataLocation location, EncryptionStatus encryption, QWidget* parent) :
	QDialog(parent),
	dataLocation(std::move(location)),
	encryptionStatus(encryption) {
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);

	languageSectionLabel = new QLabel(this);
	MakeSectionLabel(*languageSectionLabel);
	languageComboBox = new QComboBox(this);
	languageComboBox->addItem(QString());
	languageComboBox->addItem(ENGLISH_LANGUAGE_NAME);
	languageComboBox->addItem(RUSSIAN_LANGUAGE_NAME.toString());
	languageComboBox->setCurrentIndex(LanguageIndex(currentLanguage));

	storageSectionLabel = new QLabel(this);
	MakeSectionLabel(*storageSectionLabel);
	storageNoticeLabel = new QLabel(this);
	storageNoticeLabel->setObjectName(NOTICE_STYLE);
	storageNoticeLabel->setWordWrap(true);
	dataFolderTitleLabel = new QLabel(this);
	dataFolderTitleLabel->setObjectName(SECONDARY_TEXT_STYLE);
	// A field keeps a long path whole and copyable instead of breaking it inside a word.
	dataFolderEdit = new QLineEdit(QDir::toNativeSeparators(dataLocation.directoryPath), this);
	dataFolderEdit->setReadOnly(true);
	dataFolderEdit->setCursorPosition(0);
	openFolderButton = new QPushButton(this);
	portableModeLabel = new QLabel(this);
	portableModeLabel->setObjectName(SECONDARY_TEXT_STYLE);
	portableModeLabel->setWordWrap(true);
	buttonBox = new QDialogButtonBox(QDialogButtonBox::Close, this);

	auto* folderRowLayout = new QHBoxLayout();
	folderRowLayout->addWidget(dataFolderEdit, 1);
	folderRowLayout->addWidget(openFolderButton);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(languageSectionLabel);
	dialogLayout->addWidget(languageComboBox);
	dialogLayout->addSpacing(SECTION_SPACING);
	dialogLayout->addWidget(storageSectionLabel);
	dialogLayout->addWidget(storageNoticeLabel);
	dialogLayout->addWidget(dataFolderTitleLabel);
	dialogLayout->addLayout(folderRowLayout);
	dialogLayout->addWidget(portableModeLabel);
	dialogLayout->addStretch();
	dialogLayout->addWidget(buttonBox);

	connect(languageComboBox, &QComboBox::activated, this, &SettingsDialog::SelectLanguage);
	connect(openFolderButton, &QPushButton::clicked, this, &SettingsDialog::OpenDataFolder);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

	RetranslateUi();
}

void SettingsDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void SettingsDialog::RetranslateUi() {
	setWindowTitle(tr("Settings"));
	languageSectionLabel->setText(tr("Language"));
	languageComboBox->setItemText(SYSTEM_LANGUAGE_INDEX, tr("System language"));
	storageSectionLabel->setText(tr("Storage"));
	storageNoticeLabel->setObjectName(encryptionStatus == EncryptionStatus::ON ? SUCCESS_BANNER_STYLE : NOTICE_STYLE);
	switch (encryptionStatus) {
		case EncryptionStatus::OFF:
			storageNoticeLabel->setText(tr("Data is stored on this computer without encryption. Turn it on with the button at the top of the main window."));
			break;
		case EncryptionStatus::ON:
			storageNoticeLabel->setText(tr("Data on this computer is encrypted. The system keeps the key for your user account."));
			break;
		case EncryptionStatus::LOCKED:
		case EncryptionStatus::KEY_UNAVAILABLE:
			storageNoticeLabel->setText(tr("Data on this computer is encrypted, but its key is not available now."));
			break;
		case EncryptionStatus::UNSUPPORTED:
			storageNoticeLabel->setText(tr("Data is stored on this computer without encryption: the system has nowhere to keep a key."));
			break;
	}
	dataFolderTitleLabel->setText(tr("Data folder"));
	openFolderButton->setText(tr("Open folder"));
	if (dataLocation.isPortable) {
		portableModeLabel->setText(tr("Portable mode: the .portable file next to SteamForge keeps the data in this folder next to the program."));
	} else {
		portableModeLabel->setText(tr("To keep the data next to the program instead, place an empty file named .portable next to SteamForge and restart it. Existing data is not moved automatically."));
	}
	buttonBox->button(QDialogButtonBox::Close)->setText(tr("Close"));
}

void SettingsDialog::SelectLanguage(int comboIndex) {
	Q_EMIT LanguageSelected(LanguageAtIndex(comboIndex));
}

void SettingsDialog::OpenDataFolder() {
	if (!QDir().mkpath(dataLocation.directoryPath) || !QDesktopServices::openUrl(QUrl::fromLocalFile(dataLocation.directoryPath))) {
		QMessageBox::warning(this, windowTitle(), tr("The data folder could not be opened."));
	}
}

} // namespace SteamForge
