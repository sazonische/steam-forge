#include "ui/cloud_accounts_view.h"

#include "app/account_library.h"
#include "app/account_profiles.h"
#include "app/cloud_sync.h"
#include "app/google_connection.h"
#include "ui/illustration_view.h"
#include "ui/image_resources.h"
#include "ui/row_list.h"
#include "ui/steam_picture.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int VIEW_SPACING = 10;
	constexpr int ROWS_MAXIMUM_HEIGHT = 260;
	constexpr QSize CLOUD_ICON_SIZE{LIST_AVATAR_EDGE, LIST_AVATAR_EDGE};
} // namespace

CloudAccountsView::CloudAccountsView(const CloudSync& sync, const GoogleConnection& connection, const AccountLibrary& library, AccountTitleProvider titleProvider, AccountProfiles* profiles, QWidget* parent) :
	QWidget(parent),
	cloudSync(sync),
	googleConnection(connection),
	accountLibrary(library),
	accountTitleProvider(std::move(titleProvider)),
	accountProfiles(profiles) {
	accountsSectionLabel = new QLabel(this);
	MakeSectionLabel(*accountsSectionLabel);
	hintLabel = new QLabel(this);
	hintLabel->setObjectName(DETAIL_TEXT_STYLE);
	hintLabel->setWordWrap(true);

	QFrame* listFrame = CreateListFrame(this);
	auto* syncRow = new QWidget(listFrame);
	syncLabel = new QLabel(syncRow);
	syncLabel->setWordWrap(true);
	syncButton = new QPushButton(syncRow);
	MakeCompact(*syncButton);
	auto* syncLayout = new QHBoxLayout(syncRow);
	syncLayout->setContentsMargins(LIST_ROW_MARGINS);
	syncLayout->setSpacing(LIST_ROW_SPACING);
	syncLayout->addWidget(syncLabel, 1);
	syncLayout->addWidget(syncButton);
	rowList = new ScrollingRowList(ROWS_MAXIMUM_HEIGHT, listFrame);
	listFrame->layout()->addWidget(syncRow);
	listFrame->layout()->addWidget(CreateRowDivider(listFrame));
	listFrame->layout()->addWidget(rowList);

	auto* viewLayout = new QVBoxLayout(this);
	viewLayout->setContentsMargins(0, 0, 0, 0);
	viewLayout->setSpacing(VIEW_SPACING);
	viewLayout->addWidget(accountsSectionLabel);
	viewLayout->addWidget(hintLabel);
	viewLayout->addWidget(listFrame);

	rebuildTimer.setSingleShot(true);
	rebuildTimer.setInterval(0);
	connect(&rebuildTimer, &QTimer::timeout, this, &CloudAccountsView::Rebuild);
	const auto scheduleRebuild = [this]() {
		rebuildTimer.start();
	};
	connect(&sync, &CloudSync::StatusChanged, this, scheduleRebuild);
	connect(&connection, &GoogleConnection::LinksChanged, this, scheduleRebuild);
	connect(&library, &AccountLibrary::RecordsChanged, this, scheduleRebuild);
	// Avatars arrive after the window opens.
	if (accountProfiles != nullptr) {
		connect(accountProfiles, &AccountProfiles::ProfilesChanged, this, scheduleRebuild);
	}
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
	hintLabel->setText(tr("Tick the accounts to keep in Google."));
	syncButton->setText(tr("Sync now"));
	Rebuild();
}

void CloudAccountsView::Rebuild() {
	syncLabel->setText(SyncText());
	syncButton->setEnabled(cloudSync.Status() != CloudSyncStatus::SYNCING);
	rowList->Clear();
	for (const AuthenticatorRecord& record : accountLibrary.Records()) {
		if (DetermineCapabilities(record).canGenerateCodes) {
			rowList->AddRow(CreateRecordRow(record));
		}
	}
	for (const CloudOnlyRecord& record : googleConnection.CloudOnlyRecords()) {
		rowList->AddRow(CreateCloudOnlyRow(record));
	}
}

QWidget* CloudAccountsView::CreateRecordRow(const AuthenticatorRecord& record) {
	auto* avatarPicture = new PictureView(LIST_AVATAR_EDGE);
	if (accountProfiles != nullptr) {
		avatarPicture->SetPicture(accountProfiles->Profile(record.account.steamId).avatar);
	}
	const auto [row, checkBox, rowLayout] = CreateAccountChoiceRow(avatarPicture, accountTitleProvider(record), rowList);
	QLabel* stateLabel = CreateLabel(row, DETAIL_TEXT_STYLE);
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
	stateLabel->setText(isInCloud ? tr("In Google") : tr("Only here"));
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
	const auto [row, checkBox, rowLayout] = CreateAccountChoiceRow(new IllustrationView(CLOUD_CHECK_ICON_RESOURCE, CLOUD_ICON_SIZE), record.title.isEmpty() ? tr("Account") : record.title, rowList);
	// Nothing to tick: the account is not on this computer.
	checkBox->hide();
	QLabel* stateLabel = CreateLabel(row, DETAIL_TEXT_STYLE);
	stateLabel->setText(tr("Only in Google"));
	stateLabel->setToolTip(tr("Removed from this computer; it stays in Google for your other computers."));
	auto* returnButton = new QPushButton(tr("Return here"), row);
	MakeCompact(*returnButton);
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
