#include "ui/import_dialog.h"

#include "app/account_library.h"
#include "ui/illustration_view.h"
#include "ui/record_presentation.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	enum PreviewColumn {
		ACCOUNT_COLUMN,
		STEAM_ID_COLUMN,
		CODES_COLUMN,
		CONFIRMATIONS_COLUMN,
		RECOVERY_CODE_COLUMN,
		STATUS_COLUMN,
		PREVIEW_COLUMN_COUNT
	};

	constexpr int CANDIDATE_INDEX_ROLE = Qt::UserRole;
	constexpr QSize MINIMUM_DIALOG_SIZE{820, 620};
	constexpr int PASTE_EDIT_HEIGHT = 72;
	constexpr int DIALOG_SPACING = 12;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins DROP_ZONE_MARGINS{24, 20, 24, 20};
	constexpr int DROP_ZONE_SPACING = 8;
	constexpr QLatin1StringView DROP_ILLUSTRATION_RESOURCE{":/illustrations/import_files.svg"};
	constexpr QSize DROP_ILLUSTRATION_SIZE{300, 160};
	constexpr QLatin1StringView DROP_ACTIVE_PROPERTY{"dropActive"};
	constexpr std::array AUTHENTICATOR_FILE_PATTERNS{QLatin1StringView("*.maFile"), QLatin1StringView("twofactor_*.json")};

	QString YesNo(bool value) {
		return value ? QObject::tr("Yes") : QObject::tr("No");
	}
} // namespace

QStringList DroppedFilePaths(const QMimeData& mimeData) {
	QStringList nameFilters;
	for (const QLatin1StringView pattern : AUTHENTICATOR_FILE_PATTERNS) {
		nameFilters.append(pattern);
	}
	QStringList filePaths;
	for (const QUrl& url : mimeData.urls()) {
		if (!url.isLocalFile()) {
			continue;
		}
		const QFileInfo fileInfo(url.toLocalFile());
		if (fileInfo.isFile()) {
			filePaths.append(fileInfo.absoluteFilePath());
			continue;
		}
		if (!fileInfo.isDir()) {
			continue;
		}
		for (const QFileInfo& entry : QDir(fileInfo.absoluteFilePath()).entryInfoList(nameFilters, QDir::Files, QDir::Name)) {
			filePaths.append(entry.absoluteFilePath());
		}
	}
	return filePaths;
}

ImportDialog::ImportDialog(AccountLibrary& library, QWidget* parent) :
	QDialog(parent),
	accountLibrary(library) {
	setMinimumSize(MINIMUM_DIALOG_SIZE);
	setAcceptDrops(true);

	dropZone = new QFrame(this);
	dropZone->setObjectName(DROP_ZONE_STYLE);
	auto* dropIllustration = new IllustrationView(DROP_ILLUSTRATION_RESOURCE, DROP_ILLUSTRATION_SIZE, dropZone);
	dropTitleLabel = new QLabel(dropZone);
	dropTitleLabel->setObjectName(PAGE_TITLE_STYLE);
	dropTitleLabel->setAlignment(Qt::AlignCenter);
	dropHintLabel = new QLabel(dropZone);
	dropHintLabel->setObjectName(SECONDARY_TEXT_STYLE);
	dropHintLabel->setAlignment(Qt::AlignCenter);
	chooseFilesButton = new QPushButton(dropZone);
	SetButtonRole(*chooseFilesButton, ButtonRole::PRIMARY);
	formatsLabel = new QLabel(dropZone);
	formatsLabel->setObjectName(SECONDARY_TEXT_STYLE);
	formatsLabel->setAlignment(Qt::AlignCenter);
	formatsLabel->setWordWrap(true);
	auto* dropZoneLayout = new QVBoxLayout(dropZone);
	dropZoneLayout->setContentsMargins(DROP_ZONE_MARGINS);
	dropZoneLayout->setSpacing(DROP_ZONE_SPACING);
	dropZoneLayout->addStretch();
	dropZoneLayout->addWidget(dropIllustration, 0, Qt::AlignHCenter);
	dropZoneLayout->addWidget(dropTitleLabel);
	dropZoneLayout->addWidget(dropHintLabel);
	dropZoneLayout->addWidget(chooseFilesButton, 0, Qt::AlignHCenter);
	dropZoneLayout->addWidget(formatsLabel);
	dropZoneLayout->addStretch();

	listHeader = new QWidget(this);
	listTitleLabel = new QLabel(listHeader);
	MakeSectionLabel(*listTitleLabel);
	addFilesButton = new QPushButton(listHeader);
	auto* listHeaderLayout = new QHBoxLayout(listHeader);
	listHeaderLayout->setContentsMargins(0, 0, 0, 0);
	listHeaderLayout->addWidget(listTitleLabel);
	listHeaderLayout->addStretch();
	listHeaderLayout->addWidget(addFilesButton);

	pasteToggleButton = new QPushButton(this);
	pasteToggleButton->setObjectName(LINK_BUTTON_STYLE);
	pasteToggleButton->setCursor(Qt::PointingHandCursor);
	pasteArea = new QWidget(this);
	pasteLabel = new QLabel(pasteArea);
	pasteLabel->setObjectName(SECONDARY_TEXT_STYLE);
	pasteEdit = new QPlainTextEdit(pasteArea);
	pasteEdit->setFixedHeight(PASTE_EDIT_HEIGHT);
	readPastedButton = new QPushButton(pasteArea);

	previewTree = new QTreeWidget(this);
	previewTree->setColumnCount(PREVIEW_COLUMN_COUNT);
	previewTree->setRootIsDecorated(false);
	previewTree->setUniformRowHeights(true);
	previewTree->setSelectionMode(QAbstractItemView::NoSelection);
	previewTree->header()->setStretchLastSection(false);
	previewTree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
	previewTree->header()->setSectionResizeMode(ACCOUNT_COLUMN, QHeaderView::Stretch);

	summaryLabel = new QLabel(this);
	summaryLabel->setObjectName(SECONDARY_TEXT_STYLE);
	summaryLabel->setWordWrap(true);
	messageLabel = new QLabel(this);
	messageLabel->setObjectName(ERROR_TEXT_STYLE);
	messageLabel->setWordWrap(true);
	messageLabel->hide();
	googleCheckBox = new QCheckBox(this);
	googleCheckBox->setChecked(true);
	googleCheckBox->hide();

	buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	importButton = buttonBox->addButton(QString(), QDialogButtonBox::AcceptRole);
	SetButtonRole(*importButton, ButtonRole::PRIMARY);

	auto* pasteRowLayout = new QHBoxLayout();
	pasteRowLayout->addStretch();
	pasteRowLayout->addWidget(readPastedButton);
	auto* pasteAreaLayout = new QVBoxLayout(pasteArea);
	pasteAreaLayout->setContentsMargins(0, 0, 0, 0);
	pasteAreaLayout->addWidget(pasteLabel);
	pasteAreaLayout->addWidget(pasteEdit);
	pasteAreaLayout->addLayout(pasteRowLayout);
	pasteArea->hide();

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(dropZone, 1);
	dialogLayout->addWidget(listHeader);
	dialogLayout->addWidget(previewTree, 1);
	dialogLayout->addWidget(summaryLabel);
	dialogLayout->addWidget(pasteToggleButton, 0, Qt::AlignLeft);
	dialogLayout->addWidget(pasteArea);
	dialogLayout->addWidget(messageLabel);
	dialogLayout->addWidget(googleCheckBox);
	dialogLayout->addWidget(buttonBox);

	connect(chooseFilesButton, &QPushButton::clicked, this, &ImportDialog::ChooseFiles);
	connect(addFilesButton, &QPushButton::clicked, this, &ImportDialog::ChooseFiles);
	connect(pasteToggleButton, &QPushButton::clicked, this, [this]() {
		pasteArea->setVisible(!pasteArea->isVisible());
		if (pasteArea->isVisible()) {
			pasteEdit->setFocus();
		}
	});
	connect(readPastedButton, &QPushButton::clicked, this, &ImportDialog::ReadPastedLinks);
	connect(pasteEdit, &QPlainTextEdit::textChanged, this, [this]() {
		readPastedButton->setEnabled(!pasteEdit->toPlainText().trimmed().isEmpty());
	});
	connect(previewTree, &QTreeWidget::itemChanged, this, [this](const QTreeWidgetItem* item, int column) {
		if (column != ACCOUNT_COLUMN) {
			return;
		}
		const auto candidateIndex = item->data(ACCOUNT_COLUMN, CANDIDATE_INDEX_ROLE).value<std::size_t>();
		if (candidateIndex < candidates.size()) {
			candidates[candidateIndex].isChecked = item->checkState(ACCOUNT_COLUMN) == Qt::Checked;
		}
		UpdateImportButton();
	});
	connect(buttonBox, &QDialogButtonBox::accepted, this, &ImportDialog::ImportCheckedCandidates);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

	readPastedButton->setEnabled(false);
	RetranslateUi();
}

const std::vector<QUuid>& ImportDialog::ImportedRecordIds() const {
	return importedRecordIds;
}

void ImportDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void ImportDialog::OfferGoogle() {
	googleCheckBox->show();
}

bool ImportDialog::AddsToGoogle() const {
	return !googleCheckBox->isHidden() && googleCheckBox->isChecked();
}

void ImportDialog::RetranslateUi() {
	setWindowTitle(tr("Import authenticator"));
	googleCheckBox->setText(tr("Add the new accounts to Google, so they are on your other computers too"));
	dropTitleLabel->setText(tr("Drop authenticator files here"));
	dropHintLabel->setText(tr("or"));
	chooseFilesButton->setText(tr("Choose files…"));
	formatsLabel->setText(tr("A .maFile of Steam Desktop Authenticator, also encrypted next to its manifest.json, or the whole maFiles folder; twofactor_<SteamID>.json of node-steamcommunity; a WinAuth export."));
	listTitleLabel->setText(tr("Found in the files"));
	addFilesButton->setText(tr("Add files…"));
	pasteToggleButton->setText(tr("Paste otpauth:// links instead…"));
	pasteLabel->setText(tr("otpauth:// links, one per line:"));
	readPastedButton->setText(tr("Read pasted links"));
	previewTree->setHeaderLabels({
		tr("Account"),
		tr("SteamID"),
		tr("Codes"),
		tr("Confirmations"),
		tr("Recovery code"),
		tr("Status"),
	});
	buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
	RebuildPreview();
}

void ImportDialog::dragEnterEvent(QDragEnterEvent* event) {
	if (DroppedFilePaths(*event->mimeData()).isEmpty()) {
		return;
	}
	event->acceptProposedAction();
	SetDropHighlighted(true);
}

void ImportDialog::dragLeaveEvent(QDragLeaveEvent* event) {
	SetDropHighlighted(false);
	QDialog::dragLeaveEvent(event);
}

void ImportDialog::dropEvent(QDropEvent* event) {
	SetDropHighlighted(false);
	const QStringList filePaths = DroppedFilePaths(*event->mimeData());
	if (filePaths.isEmpty()) {
		return;
	}
	event->acceptProposedAction();
	AddFiles(filePaths);
}

void ImportDialog::SetDropHighlighted(bool isHighlighted) {
	dropZone->setProperty(DROP_ACTIVE_PROPERTY.data(), isHighlighted);
	dropZone->style()->unpolish(dropZone);
	dropZone->style()->polish(dropZone);
}

void ImportDialog::ChooseFiles() {
	AddFiles(QFileDialog::getOpenFileNames(this, tr("Choose authenticator files"), QString(), tr("Authenticator files (*.maFile *.json *.txt);;All files (*)")));
}

void ImportDialog::AddFiles(const QStringList& filePaths) {
	QStringList messages;
	for (const QString& filePath : filePaths) {
		const QString fileName = QFileInfo(filePath).fileName();
		std::optional<std::expected<ImportResult, ImportError>> importResult = ImportFileAskingPasskey(filePath);
		if (!importResult.has_value()) {
			messages.append(tr("%1 was skipped.").arg(fileName));
			continue;
		}
		if (!importResult->has_value()) {
			messages.append(ImportErrorText(importResult->error(), fileName));
			continue;
		}
		AddCandidates(std::move(**importResult));
	}
	RebuildPreview();
	ShowMessages(messages);
}

void ImportDialog::ReadPastedLinks() {
	std::expected<ImportResult, ImportError> importResult = ParseOtpauthLinks(pasteEdit->toPlainText(), QString());
	if (!importResult.has_value()) {
		ShowMessages({ImportErrorText(importResult.error(), tr("The pasted text"))});
		return;
	}
	AddCandidates(std::move(*importResult));
	pasteEdit->clear();
	RebuildPreview();
	ShowMessages({});
}

std::optional<std::expected<ImportResult, ImportError>> ImportDialog::ImportFileAskingPasskey(const QString& filePath) {
	const QString fileName = QFileInfo(filePath).fileName();
	std::expected<ImportResult, ImportError> importResult = ImportFile(filePath, rememberedPasskey);
	while (!importResult.has_value() && (importResult.error().kind == ImportErrorKind::PASSKEY_REQUIRED || importResult.error().kind == ImportErrorKind::PASSKEY_INCORRECT)) {
		QString prompt = tr("%1 is encrypted by Steam Desktop Authenticator. Enter its passkey:").arg(fileName);
		if (importResult.error().kind == ImportErrorKind::PASSKEY_INCORRECT) {
			prompt = tr("The passkey does not decrypt %1. Enter the passkey of Steam Desktop Authenticator:").arg(fileName);
		}
		bool isAccepted = false;
		const QString passkey = QInputDialog::getText(this, tr("Encrypted file"), prompt, QLineEdit::Password, QString(), &isAccepted);
		if (!isAccepted) {
			return std::nullopt;
		}
		importResult = ImportFile(filePath, passkey);
		if (importResult.has_value()) {
			// Steam Desktop Authenticator encrypts all files of one folder with the same passkey.
			rememberedPasskey = passkey;
		}
	}
	return importResult;
}

void ImportDialog::AddCandidates(ImportResult importResult) {
	skippedEntryCount += importResult.skippedEntryCount;
	for (AuthenticatorRecord& record : importResult.records) {
		ImportMatch match = accountLibrary.MatchExisting(record);
		if (match != ImportMatch::SAME_AUTHENTICATOR) {
			// Entries read earlier in this dialog count too, so a file chosen twice is not imported twice.
			std::vector<AuthenticatorRecord> pendingRecords;
			pendingRecords.reserve(candidates.size());
			std::ranges::transform(candidates, std::back_inserter(pendingRecords), &Candidate::record);
			const ImportMatch pendingMatch = MatchRecord(record, pendingRecords);
			if (pendingMatch == ImportMatch::SAME_AUTHENTICATOR || match == ImportMatch::NEW_ACCOUNT) {
				match = pendingMatch;
			}
		}
		std::optional<QUuid> completedRecordId;
		if (match == ImportMatch::SAME_AUTHENTICATOR) {
			completedRecordId = FindCompletedRecord(record);
		}
		const bool isChecked = match == ImportMatch::NEW_ACCOUNT || completedRecordId.has_value();
		candidates.push_back({
			.record = std::move(record),
			.match = match,
			.completedRecordId = completedRecordId,
			.isChecked = isChecked,
		});
	}
}

void ImportDialog::RebuildPreview() {
	const QSignalBlocker treeSignalBlocker(previewTree);
	previewTree->clear();
	bool hasExistingMatches = false;
	bool hasCompletions = false;
	for (std::size_t candidateIndex = 0; candidateIndex < candidates.size(); ++candidateIndex) {
		const Candidate& candidate = candidates[candidateIndex];
		const RecordCapabilities capabilities = DetermineCapabilities(candidate.record);
		auto* item = new QTreeWidgetItem(previewTree);
		item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
		item->setData(ACCOUNT_COLUMN, CANDIDATE_INDEX_ROLE, QVariant::fromValue(candidateIndex));
		item->setCheckState(ACCOUNT_COLUMN, candidate.isChecked ? Qt::Checked : Qt::Unchecked);
		item->setText(ACCOUNT_COLUMN, RecordDisplayName(candidate.record));
		item->setText(STEAM_ID_COLUMN, candidate.record.account.steamId.has_value() ? QString::number(*candidate.record.account.steamId) : tr("unknown"));
		item->setText(CODES_COLUMN, YesNo(capabilities.canGenerateCodes));
		item->setText(CONFIRMATIONS_COLUMN, YesNo(capabilities.canSignConfirmations));
		item->setText(RECOVERY_CODE_COLUMN, YesNo(capabilities.hasRevocationCode));
		switch (candidate.match) {
			case ImportMatch::NEW_ACCOUNT:
				item->setText(STATUS_COLUMN, tr("New"));
				break;
			case ImportMatch::SAME_AUTHENTICATOR:
				if (candidate.completedRecordId.has_value()) {
					item->setText(STATUS_COLUMN, tr("Adds missing data to the saved record"));
					hasCompletions = true;
				} else {
					item->setText(STATUS_COLUMN, tr("Already added"));
					hasExistingMatches = true;
				}
				break;
			case ImportMatch::OTHER_AUTHENTICATOR_FOR_ACCOUNT:
				item->setText(STATUS_COLUMN, tr("Other authenticator of this account saved"));
				hasExistingMatches = true;
				break;
		}
	}

	QStringList summaryParts;
	if (hasCompletions) {
		summaryParts.append(tr("An entry of a saved authenticator only adds what the saved record lacks, such as the confirmation key or the SteamID. Saved values are never replaced."));
	}
	if (hasExistingMatches) {
		summaryParts.append(tr("Entries that match saved records are imported only if you check them. A checked entry is saved as a separate record, and existing records are never replaced."));
	}
	if (skippedEntryCount > 0) {
		summaryParts.append(tr("Entries of other services skipped: %n.", nullptr, skippedEntryCount));
	}
	summaryLabel->setText(summaryParts.join(QLatin1Char(' ')));
	summaryLabel->setVisible(!summaryParts.isEmpty());
	const bool hasCandidates = !candidates.empty();
	dropZone->setVisible(!hasCandidates);
	listHeader->setVisible(hasCandidates);
	previewTree->setVisible(hasCandidates);
	UpdateImportButton();
}

void ImportDialog::UpdateImportButton() {
	const auto checkedCount = static_cast<int>(std::ranges::count_if(candidates, &Candidate::isChecked));
	importButton->setText(tr("Import (%n)", nullptr, checkedCount));
	importButton->setEnabled(checkedCount > 0);
}

void ImportDialog::ImportCheckedCandidates() {
	for (const Candidate& candidate : candidates) {
		if (!candidate.isChecked) {
			continue;
		}
		if (candidate.completedRecordId.has_value()) {
			// Completed against the record as saved now: an entry chosen twice must not undo the first one.
			const AuthenticatorRecord* savedRecord = accountLibrary.FindRecord(*candidate.completedRecordId);
			const std::optional<AuthenticatorRecord> completedRecord = savedRecord != nullptr ? CompleteRecord(*savedRecord, candidate.record) : std::nullopt;
			if (completedRecord.has_value()) {
				if (const std::expected<void, StorageError> saved = accountLibrary.Save(*completedRecord); !saved.has_value()) {
					QMessageBox::critical(this, windowTitle(), StorageErrorText(saved.error()));
					break;
				}
			}
			importedRecordIds.push_back(*candidate.completedRecordId);
			continue;
		}
		const std::expected<void, StorageError> added = accountLibrary.Add(candidate.record);
		if (!added.has_value()) {
			QMessageBox::critical(this, windowTitle(), StorageErrorText(added.error()));
			break;
		}
		importedRecordIds.push_back(candidate.record.id);
	}
	if (importedRecordIds.empty()) {
		return;
	}
	accept();
}

std::optional<QUuid> ImportDialog::FindCompletedRecord(const AuthenticatorRecord& importedRecord) const {
	for (const AuthenticatorRecord& savedRecord : accountLibrary.Records()) {
		if (CompleteRecord(savedRecord, importedRecord).has_value()) {
			return savedRecord.id;
		}
	}
	return std::nullopt;
}

void ImportDialog::ShowMessages(const QStringList& messages) {
	messageLabel->setText(messages.join(QLatin1Char('\n')));
	messageLabel->setVisible(!messages.isEmpty());
}

} // namespace SteamForge
