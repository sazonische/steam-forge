#pragma once

#include "app/interface_translator.h"
#include "app/storage_encryption.h"
#include "platform/data_location.h"

namespace SteamForge {

class SettingsDialog final : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(std::optional<InterfaceLanguage> currentLanguage, bool checksForUpdates, DataLocation location, EncryptionStatus encryption, QWidget* parent = nullptr);

Q_SIGNALS:
	void LanguageSelected(std::optional<InterfaceLanguage> language);
	void UpdateChecksToggled(bool isEnabled);

protected:
	void changeEvent(QEvent* event) override;

private:
	void RetranslateUi();
	void SelectLanguage(int comboIndex);
	void OpenDataFolder();

	DataLocation dataLocation;
	EncryptionStatus encryptionStatus;

	QLabel* languageSectionLabel = nullptr;
	QComboBox* languageComboBox = nullptr;
	QLabel* storageSectionLabel = nullptr;
	QLabel* storageNoticeLabel = nullptr;
	QLabel* dataFolderTitleLabel = nullptr;
	QLineEdit* dataFolderEdit = nullptr;
	QPushButton* openFolderButton = nullptr;
	QLabel* portableModeLabel = nullptr;
	QLabel* updatesSectionLabel = nullptr;
	QCheckBox* updateChecksCheckBox = nullptr;
	QLabel* updatesNoteLabel = nullptr;
	QDialogButtonBox* buttonBox = nullptr;
};

} // namespace SteamForge
