#pragma once

#include "domain/authenticator_record.h"
#include "storage/authenticator_import.h"

namespace SteamForge {

class AccountLibrary;
class IllustrationView;

// Local files of a drop; a folder stands for the authenticator files in it, as the maFiles folder of Steam Desktop Authenticator.
[[nodiscard]] QStringList DroppedFilePaths(const QMimeData& mimeData);

class ImportDialog final : public QDialog {
	Q_OBJECT

public:
	explicit ImportDialog(AccountLibrary& library, QWidget* parent = nullptr);

	[[nodiscard]] const std::vector<QUuid>& ImportedRecordIds() const;
	void AddFiles(const QStringList& filePaths);
	// While Google is connected: a ticked box adds the new accounts to Google.
	void OfferGoogle();
	[[nodiscard]] bool AddsToGoogle() const;

protected:
	void changeEvent(QEvent* event) override;
	void dragEnterEvent(QDragEnterEvent* event) override;
	void dragLeaveEvent(QDragLeaveEvent* event) override;
	void dropEvent(QDropEvent* event) override;

private:
	struct Candidate {
		AuthenticatorRecord record;
		ImportMatch match = ImportMatch::NEW_ACCOUNT;
		// The saved record of the same authenticator this entry adds missing data to.
		std::optional<QUuid> completedRecordId;
		bool isChecked = false;
	};

	void RetranslateUi();
	void ChooseFiles();
	void ReadPastedLinks();
	// Empty when the user cancels the passkey prompt.
	[[nodiscard]] std::optional<std::expected<ImportResult, ImportError>> ImportFileAskingPasskey(const QString& filePath);
	void AddCandidates(ImportResult importResult);
	void RebuildPreview();
	void UpdateImportButton();
	void ImportCheckedCandidates();
	void ShowMessages(const QStringList& messages);
	void SetDropHighlighted(bool isHighlighted);
	[[nodiscard]] std::optional<QUuid> FindCompletedRecord(const AuthenticatorRecord& importedRecord) const;

	AccountLibrary& accountLibrary;
	std::vector<Candidate> candidates;
	std::vector<QUuid> importedRecordIds;
	std::optional<QString> rememberedPasskey;
	int skippedEntryCount = 0;

	QFrame* dropZone = nullptr;
	QLabel* dropTitleLabel = nullptr;
	QLabel* dropHintLabel = nullptr;
	QPushButton* chooseFilesButton = nullptr;
	QLabel* formatsLabel = nullptr;
	QWidget* listHeader = nullptr;
	QLabel* listTitleLabel = nullptr;
	QPushButton* addFilesButton = nullptr;
	QPushButton* pasteToggleButton = nullptr;
	QWidget* pasteArea = nullptr;
	QLabel* pasteLabel = nullptr;
	QPlainTextEdit* pasteEdit = nullptr;
	QPushButton* readPastedButton = nullptr;
	QTreeWidget* previewTree = nullptr;
	QLabel* summaryLabel = nullptr;
	QLabel* messageLabel = nullptr;
	QCheckBox* googleCheckBox = nullptr;
	QDialogButtonBox* buttonBox = nullptr;
	QPushButton* importButton = nullptr;
};

} // namespace SteamForge
