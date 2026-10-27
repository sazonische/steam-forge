#include "ui/cloud_accounts_view.h"

#include "app/account_library.h"
#include "app/cloud_sync.h"
#include "app/google_connection.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int VIEW_SPACING = 10;
	constexpr int ROW_SPACING = 8;
	constexpr int ROWS_SPACING = 6;
	constexpr int ROWS_MAXIMUM_HEIGHT = 220;
} // namespace

CloudAccountsView::CloudAccountsView(const CloudSync& sync, const GoogleConnection& connection, const AccountLibrary& library, AccountTitleProvider titleProvider, QWidget* parent) :
	QWidget(parent),
	cloudSync(sync),
	googleConnection(connection),
	accountLibrary(library),
	accountTitleProvider(std::move(titleProvider)) {
	accountsSectionLabel = new QLabel(this);
	MakeSectionLabel(*accountsSectionLabel);
	hintLabel = new QLabel(this);
	hintLabel->setObjectName(SECONDARY_TEXT_STYLE);
	hintLabel->setWordWrap(true);
	syncLabel = new QLabel(this);
	syncLabel->setWordWrap(true);
	syncButton = new QPushButton(this);
	MakeCompact(*syncButton);
	auto* syncLayout = new QHBoxLayout();
	syncLayout->addWidget(syncLabel, 1);
	syncLayout->addWidget(syncButton);

	rowsWidget = new QWidget(this);
	rowsLayout = new QVBoxLayout(rowsWidget);
	rowsLayout->setContentsMargins(0, 0, 0, 0);
	rowsLayout->setSpacing(ROWS_SPACING);
	auto* rowsArea = new QScrollArea(this);
	rowsArea->setObjectName(DETAILS_SCROLL_STYLE);
	rowsArea->setFrameShape(QFrame::NoFrame);
	rowsArea->setWidgetResizable(true);
	rowsArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	rowsArea->setMaximumHeight(ROWS_MAXIMUM_HEIGHT);
	rowsArea->setWidget(rowsWidget);

	auto* viewLayout = new QVBoxLayout(this);
	viewLayout->setContentsMargins(0, 0, 0, 0);
	viewLayout->setSpacing(VIEW_SPACING);
	viewLayout->addWidget(accountsSectionLabel);
	viewLayout->addWidget(hintLabel);
	viewLayout->addLayout(syncLayout);
	viewLayout->addWidget(rowsArea);

	rebuildTimer.setSingleShot(true);
	rebuildTimer.setInterval(0);
	connect(&rebuildTimer, &QTimer::timeout, this, &CloudAccountsView::Rebuild);
	const auto scheduleRebuild = [this]() {
		rebuildTimer.start();
	};
	connect(&sync, &CloudSync::StatusChanged, this, scheduleRebuild);
	connect(&connection, &GoogleConnection::LinksChanged, this, scheduleRebuild);
	connect(&library, &AccountLibrary::RecordsChanged, this, scheduleRebuild);
	connect(syncButton, &QPushButton::clicked, this, &CloudAccountsView::SyncRequested);
	RetranslateUi();
}

void CloudAccountsView::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QWidget::changeEvent(event);
}

void CloudAccountsView::RetranslateUi() {
	accountsSectionLabel->setText(tr("Accounts"));
	hintLabel->setText(tr("Ticked accounts are saved in your Google and appear on every computer where you connect it. Without the tick an account stays only on this computer."));
	syncButton->setText(tr("Sync now"));
	Rebuild();
}

void CloudAccountsView::Rebuild() {
	syncLabel->setText(SyncText());
	syncButton->setEnabled(cloudSync.Status() != CloudSyncStatus::SYNCING);
	// The row that asked for the change may still be on the stack.
	while (const QLayoutItem* item = rowsLayout->takeAt(0)) {
		if (QWidget* row = item->widget(); row != nullptr) {
			row->hide();
			row->deleteLater();
		}
		delete item;
	}
	for (const AuthenticatorRecord& record : accountLibrary.Records()) {
		if (DetermineCapabilities(record).canGenerateCodes) {
			rowsLayout->addWidget(CreateRecordRow(record));
		}
	}
	for (const CloudOnlyRecord& record : googleConnection.CloudOnlyRecords()) {
		rowsLayout->addWidget(CreateCloudOnlyRow(record));
	}
	rowsLayout->addStretch();
}

QWidget* CloudAccountsView::CreateRecordRow(const AuthenticatorRecord& record) {
	auto* row = new QWidget(rowsWidget);
	auto* checkBox = new QCheckBox(accountTitleProvider(record), row);
	auto* stateLabel = new QLabel(row);
	stateLabel->setObjectName(SECONDARY_TEXT_STYLE);
	auto* rowLayout = new QHBoxLayout(row);
	rowLayout->setContentsMargins(0, 0, 0, 0);
	rowLayout->setSpacing(ROW_SPACING);
	rowLayout->addWidget(checkBox, 1);
	rowLayout->addWidget(stateLabel);

	const QUuid recordId = record.id;
	if (cloudSync.HasConflict(recordId)) {
		checkBox->setEnabled(false);
		stateLabel->setText(tr("Google has another version"));
		auto* chooseButton = new QPushButton(tr("Choose…"), row);
		MakeCompact(*chooseButton);
		SetButtonRole(*chooseButton, ButtonRole::PRIMARY);
		rowLayout->addWidget(chooseButton);
		connect(chooseButton, &QPushButton::clicked, this, [this, recordId]() {
			Q_EMIT ConflictRequested(recordId);
		});
		return row;
	}
	const bool isInCloud = cloudSync.IsInCloud(recordId);
	checkBox->setChecked(isInCloud);
	if (cloudSync.IsChanging(recordId)) {
		checkBox->setEnabled(false);
		stateLabel->setText(tr("Updating…"));
		return row;
	}
	stateLabel->setText(isInCloud ? tr("In Google") : tr("Only on this computer"));
	connect(checkBox, &QCheckBox::clicked, this, [this, recordId](bool isChecked) {
		if (isChecked) {
			Q_EMIT AddRequested(recordId);
		} else {
			Q_EMIT RemoveRequested(recordId);
		}
		// A removal the user cancels puts the tick back.
		rebuildTimer.start();
	});
	return row;
}

QWidget* CloudAccountsView::CreateCloudOnlyRow(const CloudOnlyRecord& record) {
	auto* row = new QWidget(rowsWidget);
	auto* nameLabel = new QLabel(record.title.isEmpty() ? tr("Account") : record.title, row);
	nameLabel->setEnabled(false);
	auto* stateLabel = new QLabel(tr("Only in Google: removed from this computer"), row);
	stateLabel->setObjectName(SECONDARY_TEXT_STYLE);
	auto* returnButton = new QPushButton(tr("Return here"), row);
	MakeCompact(*returnButton);
	auto* rowLayout = new QHBoxLayout(row);
	rowLayout->setContentsMargins(0, 0, 0, 0);
	rowLayout->setSpacing(ROW_SPACING);
	rowLayout->addWidget(nameLabel, 1);
	rowLayout->addWidget(stateLabel);
	rowLayout->addWidget(returnButton);
	connect(returnButton, &QPushButton::clicked, this, [this, cloudRecordId = record.cloudRecordId]() {
		Q_EMIT ReturnRequested(cloudRecordId);
	});
	return row;
}

QString CloudAccountsView::SyncText() const {
	const QDateTime& syncedAt = cloudSync.LastSyncedAt();
	switch (cloudSync.Status()) {
		case CloudSyncStatus::INACTIVE:
			return {};
		case CloudSyncStatus::SYNCING:
			return tr("Syncing…");
		case CloudSyncStatus::SYNCED:
			return syncedAt.isValid() ? tr("Synced at %1.").arg(QLocale().toString(syncedAt.toLocalTime().time(), QLocale::ShortFormat)) : QString();
		case CloudSyncStatus::NEEDS_DECISION:
			return tr("Google has another version of an account: choose which one to keep. The other accounts keep syncing.");
		case CloudSyncStatus::UNREACHABLE:
			return tr("Google could not be reached. SteamForge tries again in a few minutes.");
		case CloudSyncStatus::NOT_SAVED:
			return tr("A change could not be saved on this computer, so not everything is synced. SteamForge tries again at the next sync.");
		case CloudSyncStatus::UNREADABLE_IN_CLOUD:
			return tr("An account in Google could not be read: its file is damaged or was saved by a newer version of SteamForge. The accounts on this computer stay as they are.");
	}
	return {};
}

} // namespace SteamForge
