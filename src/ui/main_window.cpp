#include "ui/main_window.h"

#include "app/account_library.h"
#include "app/account_profiles.h"
#include "app/application_settings.h"
#include "app/cloud_sync.h"
#include "app/confirmation_monitor.h"
#include "app/google_connection.h"
#include "app/storage_encryption.h"
#include "app/update_check.h"
#include "common/constants.h"
#include "common/version.h"
#include "platform/desktop_notifications.h"
#include "steam/steam_clock.h"
#include "storage/authenticator_export.h"
#include "storage/private_file.h"
#include "ui/account_list_view.h"
#include "ui/cloud_conflict_dialog.h"
#include "ui/confirmations_dialog.h"
#include "ui/google_connect_dialog.h"
#include "ui/guard_setup_dialog.h"
#include "ui/illustration_view.h"
#include "ui/import_dialog.h"
#include "ui/login_approval_dialog.h"
#include "ui/record_data_dialog.h"
#include "ui/record_presentation.h"
#include "ui/settings_dialog.h"
#include "ui/steam_guard_qr_dialog.h"
#include "ui/system_tray.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr QSize STATUS_ICON_SIZE{18, 18};
	constexpr QLatin1StringView LOCK_ICON_RESOURCE{":/styles/lock.svg"};
	constexpr QLatin1StringView LOCK_OPEN_ICON_RESOURCE{":/styles/lock-open.svg"};
	constexpr QLatin1StringView LOCK_ALERT_ICON_RESOURCE{":/styles/lock-alert.svg"};
	constexpr QLatin1StringView CLOUD_ICON_RESOURCE{":/styles/cloud.svg"};
	constexpr QLatin1StringView CLOUD_CHECK_ICON_RESOURCE{":/styles/cloud-check.svg"};
	constexpr QLatin1StringView CLOUD_OFF_ICON_RESOURCE{":/styles/cloud-off.svg"};
	constexpr QLatin1StringView CLOUD_ALERT_ICON_RESOURCE{":/styles/cloud-alert.svg"};
	// A second of difference is usual and changes no code.
	constexpr qint64 NOTICEABLE_CLOCK_OFFSET_SECONDS = 2;
	constexpr QSize INITIAL_WINDOW_SIZE{1024, 680};
	constexpr QSize MINIMUM_WINDOW_SIZE{820, 560};
	constexpr int ACCOUNT_LIST_WIDTH = 440;
	constexpr int EMPTY_STATE_TEXT_WIDTH = 520;
	constexpr int EMPTY_STATE_SPACING = 12;
	constexpr int EMPTY_STATE_BUTTON_SPACING = 24;
	constexpr int EMPTY_STATE_BUTTONS_GAP = 8;
	constexpr QLatin1StringView WELCOME_ILLUSTRATION_RESOURCE{":/illustrations/welcome.svg"};
	constexpr QSize WELCOME_ILLUSTRATION_SIZE{360, 220};
	constexpr QLatin1StringView LOCKED_ILLUSTRATION_RESOURCE{":/illustrations/locked.svg"};
	constexpr QSize LOCKED_ILLUSTRATION_SIZE{360, 190};
	constexpr QMargins TOP_BAR_MARGINS{20, 10, 16, 10};
	constexpr QMargins BANNER_MARGINS{16, 12, 16, 12};
	constexpr int TOP_BAR_SPACING = 8;
	// More would make a notification too long to read at a glance.
	constexpr std::size_t NOTIFIED_CONFIRMATION_LIMIT = 3;
	// Coming back to the window after this long syncs at once instead of waiting for the next round.
	constexpr qint64 ACTIVATION_SYNC_AGE_SECONDS = 60;

	QString BulletLines(const QStringList& lines) {
		QStringList bulletLines;
		for (const QString& line : lines) {
			bulletLines.append(QStringLiteral("• ") + line);
		}
		return bulletLines.join(QLatin1Char('\n'));
	}

	void ShowStatus(QPushButton& statusButton, QLatin1StringView iconResource, const QString& text, bool needsAttention) {
		statusButton.setIcon(QIcon(iconResource));
		statusButton.setText(text);
		SetStyleFlag(statusButton, StyleFlag::ATTENTION, needsAttention);
	}

	QString ConfirmationLine(const PendingConfirmation& confirmation) {
		if (confirmation.headline.isEmpty()) {
			return ConfirmationTypeText(confirmation);
		}
		return QObject::tr("%1: %2").arg(ConfirmationTypeText(confirmation), confirmation.headline);
	}
} // namespace

MainWindow::MainWindow(AccountLibrary& library, const SteamClock& clock, ConfirmationMonitor& monitor, SteamImageCache& images, AccountProfiles& profiles, StorageEncryption& encryption, GoogleConnection& google, CloudSync& sync, ApplicationSettings& applicationSettings, DataLocation location, QWidget* parent) :
	QMainWindow(parent),
	accountLibrary(library),
	steamClock(clock),
	confirmationMonitor(monitor),
	steamImageCache(images),
	accountProfiles(profiles),
	storageEncryption(encryption),
	googleConnection(google),
	cloudSync(sync),
	settings(applicationSettings),
	dataLocation(std::move(location)) {
	setWindowTitle(APPLICATION_NAME);
	setMinimumSize(MINIMUM_WINDOW_SIZE);
	setAcceptDrops(true);
	if (!restoreGeometry(applicationSettings.MainWindowGeometry())) {
		resize(INITIAL_WINDOW_SIZE);
	}

	auto* centralWidget = new QWidget(this);
	pageStack = new QStackedWidget(centralWidget);
	emptyStatePage = CreateEmptyStatePage();
	accountsPage = CreateAccountsPage();
	lockedPage = CreateLockedPage();
	pageStack->addWidget(emptyStatePage);
	pageStack->addWidget(accountsPage);
	pageStack->addWidget(lockedPage);

	loadProblemBanner = new QWidget(centralWidget);
	loadProblemLabel = new QLabel(loadProblemBanner);
	loadProblemLabel->setObjectName(PROBLEM_BANNER_STYLE);
	loadProblemLabel->setWordWrap(true);
	loadProblemDetailsButton = new QPushButton(loadProblemBanner);
	auto* bannerLayout = new QHBoxLayout(loadProblemBanner);
	bannerLayout->setContentsMargins(BANNER_MARGINS);
	bannerLayout->addWidget(loadProblemLabel, 1);
	bannerLayout->addWidget(loadProblemDetailsButton);

	auto* centralLayout = new QVBoxLayout(centralWidget);
	centralLayout->setContentsMargins(0, 0, 0, 0);
	centralLayout->setSpacing(0);
	centralLayout->addWidget(CreateTopBar());
	centralLayout->addWidget(loadProblemBanner);
	centralLayout->addWidget(pageStack, 1);
	setCentralWidget(centralWidget);

	timeStatusLabel = new QLabel(this);
	statusBar()->addWidget(timeStatusLabel, 1);
	statusBar()->setSizeGripEnabled(false);

	connect(&library, &AccountLibrary::RecordsChanged, this, &MainWindow::UpdatePages);
	connect(&library, &AccountLibrary::SessionChanged, this, &MainWindow::RefreshShownRecord);
	connect(&clock, &SteamClock::StateChanged, this, &MainWindow::UpdateTimeStatus);
	connect(loadProblemDetailsButton, &QPushButton::clicked, this, &MainWindow::ShowLoadProblemDetails);
	connect(&monitor, &ConfirmationMonitor::AccountsChanged, this, &MainWindow::ConnectConfirmations);
	connect(&monitor, &ConfirmationMonitor::ConfirmationsArrived, this, &MainWindow::NotifyConfirmations);
	connect(&monitor, &ConfirmationMonitor::SignInRequired, this, &MainWindow::NotifySignInRequired);
	connect(&sync, &CloudSync::StatusChanged, this, &MainWindow::UpdateCloudStates);
	connect(&google, &GoogleConnection::LinksChanged, this, &MainWindow::UpdateCloudStates);
	connect(&sync, &CloudSync::RecordAdded, this, [this](const QUuid& recordId) {
		arrivedRecordIds.push_back(recordId);
	});
	connect(&sync, &CloudSync::RecordUnlinked, this, [this](const QUuid& recordId) {
		if (const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId); record != nullptr) {
			Notify(tr("Removed from Google"), tr("%1 was removed from Google on another computer. It stays here, but no longer syncs.").arg(AccountTitleFor(*record)));
		}
	});
	connect(&sync, &CloudSync::KeyReplaced, this, [this](const QUuid& recordId) {
		if (const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId); record != nullptr) {
			Notify(tr("Steam Guard replaced"), tr("%1 now has the Steam Guard from Google. The previous one is saved in the backups folder.").arg(AccountTitleFor(*record)));
		}
	});

	CreateSystemTray();
	desktopNotifications = new DesktopNotifications(systemTray != nullptr ? &systemTray->Icon() : nullptr, this);
	connect(desktopNotifications, &DesktopNotifications::Activated, this, &MainWindow::OpenConfirmationsFromNotification);
	RetranslateUi();
	UpdatePages();
}

void MainWindow::BringToFront() {
	// showNormal() would also drop the maximized state.
	setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
	show();
	raise();
	activateWindow();
}

void MainWindow::QuitApplication() {
	isQuitRequested = true;
	if (systemTray != nullptr) {
		systemTray->Hide();
	}
	close();
	// Quit explicitly: a stray visible dialog must not keep the process alive without a window.
	QCoreApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* event) {
	settings.SetMainWindowGeometry(saveGeometry());
	if (!isQuitRequested && systemTray != nullptr && systemTray->IsShown()) {
		hide();
		event->ignore();
		return;
	}
	QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
	if (!DroppedFilePaths(*event->mimeData()).isEmpty()) {
		event->acceptProposedAction();
	}
}

void MainWindow::dropEvent(QDropEvent* event) {
	const QStringList filePaths = DroppedFilePaths(*event->mimeData());
	if (filePaths.isEmpty()) {
		return;
	}
	event->acceptProposedAction();
	// Deferred: a modal dialog must not run inside the drop handler of the platform drag loop.
	QTimer::singleShot(0, this, [this, filePaths]() {
		RunImportDialog(filePaths);
	});
}

void MainWindow::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	if (event->type() == QEvent::ActivationChange && isActiveWindow()) {
		QTimer::singleShot(0, this, &MainWindow::OfferPendingUpdate);
		SyncIfStale();
	}
	QMainWindow::changeEvent(event);
}

QWidget* MainWindow::CreateTopBar() {
	auto* topBar = new QWidget(this);
	topBar->setObjectName(TOP_BAR_STYLE);
	topBar->setAttribute(Qt::WA_StyledBackground, true);
	topBarTitleLabel = new QLabel(topBar);
	topBarTitleLabel->setObjectName(TOP_BAR_TITLE_STYLE);
	// Statuses, not buttons: they tell the state and open its settings, so "Add account" stays the only call to action.
	for (QPushButton** statusButton : {&encryptionStatusButton, &googleStatusButton}) {
		*statusButton = new QPushButton(topBar);
		(*statusButton)->setObjectName(STATUS_CHIP_STYLE);
		(*statusButton)->setIconSize(STATUS_ICON_SIZE);
		(*statusButton)->setCursor(Qt::PointingHandCursor);
	}
	addAccountButton = new QPushButton(topBar);
	SetButtonRole(*addAccountButton, ButtonRole::PRIMARY);
	settingsButton = new QPushButton(topBar);
	addAccountMenu = new QMenu(addAccountButton);
	signInAction = addAccountMenu->addAction(QString());
	importAction = addAccountMenu->addAction(QString());

	auto* topBarLayout = new QHBoxLayout(topBar);
	topBarLayout->setContentsMargins(TOP_BAR_MARGINS);
	topBarLayout->setSpacing(TOP_BAR_SPACING);
	topBarLayout->addWidget(topBarTitleLabel);
	topBarLayout->addStretch();
	topBarLayout->addWidget(encryptionStatusButton);
	topBarLayout->addWidget(googleStatusButton);
	topBarLayout->addWidget(addAccountButton);
	topBarLayout->addWidget(settingsButton);

	connect(encryptionStatusButton, &QPushButton::clicked, this, &MainWindow::OpenEncryptionStatus);
	connect(googleStatusButton, &QPushButton::clicked, this, &MainWindow::OpenGoogleStatus);
	connect(&storageEncryption, &StorageEncryption::StatusChanged, this, &MainWindow::UpdateStatusButtons);
	connect(&googleConnection, &GoogleConnection::StateChanged, this, &MainWindow::UpdateCloudStates);
	connect(&storageEncryption, &StorageEncryption::StatusChanged, this, &MainWindow::UpdatePages);
	connect(addAccountButton, &QPushButton::clicked, this, &MainWindow::ShowAddAccountMenu);
	connect(signInAction, &QAction::triggered, this, &MainWindow::OpenNewAccountLogin);
	connect(importAction, &QAction::triggered, this, &MainWindow::OpenImportDialog);
	connect(settingsButton, &QPushButton::clicked, this, [this]() {
		OpenSettingsDialog(SettingsSection::GENERAL);
	});
	return topBar;
}

void MainWindow::ShowAvailableUpdate(const QVersionNumber& version) {
	if (QVersionNumber::compare(version, settings.SkippedUpdateVersion()) <= 0 || QVersionNumber::compare(version, postponedUpdateVersion) <= 0) {
		return;
	}
	pendingUpdateVersion = version;
	// Not inside the handler of the network reply: the offer runs its own event loop.
	QTimer::singleShot(0, this, &MainWindow::OfferPendingUpdate);
}

void MainWindow::OfferPendingUpdate() {
	// Hidden in the tray, behind another program or busy with a dialog, the offer waits until the window is in front again.
	if (pendingUpdateVersion.isNull() || !isActiveWindow() || QApplication::activeModalWidget() != nullptr) {
		return;
	}
	const QVersionNumber version = std::exchange(pendingUpdateVersion, QVersionNumber());
	QMessageBox offer(QMessageBox::Information, tr("Update available"), tr("SteamForge %1 is out").arg(version.toString()), QMessageBox::NoButton, this);
	offer.setInformativeText(tr("You have %1. Replace the old file with the new one: accounts and settings are kept.").arg(APPLICATION_VERSION));
	QPushButton* downloadButton = offer.addButton(tr("Download"), QMessageBox::AcceptRole);
	SetButtonRole(*downloadButton, ButtonRole::PRIMARY);
	const QPushButton* skipButton = offer.addButton(tr("Skip this version"), QMessageBox::DestructiveRole);
	offer.addButton(tr("Later"), QMessageBox::RejectRole);
	offer.setDefaultButton(downloadButton);
	offer.exec();

	if (offer.clickedButton() == downloadButton) {
		const QUrl releasePageUrl = ReleasePageUrl(version);
		if (!QDesktopServices::openUrl(releasePageUrl)) {
			QMessageBox::warning(this, tr("Update available"), tr("The browser could not be opened. The new version is at %1").arg(releasePageUrl.toString()));
		}
		return;
	}
	if (offer.clickedButton() == skipButton) {
		settings.SetSkippedUpdateVersion(version);
		return;
	}
	// Later: asked again at the next start, or when an even newer version is out.
	postponedUpdateVersion = version;
}

QWidget* MainWindow::CreateEmptyStatePage() {
	auto* page = new QWidget(pageStack);
	auto* illustration = new IllustrationView(WELCOME_ILLUSTRATION_RESOURCE, WELCOME_ILLUSTRATION_SIZE, page);
	emptyStateTitleLabel = new QLabel(page);
	emptyStateTitleLabel->setObjectName(EMPTY_STATE_TITLE_STYLE);
	emptyStateTitleLabel->setAlignment(Qt::AlignCenter);
	emptyStateTextLabel = new QLabel(page);
	emptyStateTextLabel->setObjectName(EMPTY_STATE_TEXT_STYLE);
	emptyStateTextLabel->setAlignment(Qt::AlignCenter);
	emptyStateTextLabel->setWordWrap(true);
	emptyStateTextLabel->setFixedWidth(EMPTY_STATE_TEXT_WIDTH);
	emptyStateSignInButton = new QPushButton(page);
	SetButtonRole(*emptyStateSignInButton, ButtonRole::PRIMARY);
	emptyStateImportButton = new QPushButton(page);
	auto* emptyStateButtonsLayout = new QHBoxLayout();
	emptyStateButtonsLayout->setSpacing(EMPTY_STATE_BUTTONS_GAP);
	emptyStateButtonsLayout->addStretch();
	emptyStateButtonsLayout->addWidget(emptyStateSignInButton);
	emptyStateButtonsLayout->addWidget(emptyStateImportButton);
	emptyStateButtonsLayout->addStretch();

	auto* pageLayout = new QVBoxLayout(page);
	pageLayout->setSpacing(EMPTY_STATE_SPACING);
	pageLayout->addStretch();
	pageLayout->addWidget(illustration, 0, Qt::AlignHCenter);
	pageLayout->addWidget(emptyStateTitleLabel, 0, Qt::AlignHCenter);
	// Centered by stretches: an aligned item would lose the height of its wrapped lines.
	auto* emptyStateTextLayout = new QHBoxLayout();
	emptyStateTextLayout->addStretch();
	emptyStateTextLayout->addWidget(emptyStateTextLabel);
	emptyStateTextLayout->addStretch();
	pageLayout->addLayout(emptyStateTextLayout);
	pageLayout->addSpacing(EMPTY_STATE_BUTTON_SPACING);
	pageLayout->addLayout(emptyStateButtonsLayout);
	pageLayout->addStretch();

	connect(emptyStateSignInButton, &QPushButton::clicked, this, &MainWindow::OpenNewAccountLogin);
	connect(emptyStateImportButton, &QPushButton::clicked, this, &MainWindow::OpenImportDialog);
	return page;
}

QWidget* MainWindow::CreateLockedPage() {
	auto* page = new QWidget(pageStack);
	auto* illustration = new IllustrationView(LOCKED_ILLUSTRATION_RESOURCE, LOCKED_ILLUSTRATION_SIZE, page);
	lockedTitleLabel = new QLabel(page);
	lockedTitleLabel->setObjectName(EMPTY_STATE_TITLE_STYLE);
	lockedTitleLabel->setAlignment(Qt::AlignCenter);
	lockedTextLabel = new QLabel(page);
	lockedTextLabel->setObjectName(EMPTY_STATE_TEXT_STYLE);
	lockedTextLabel->setAlignment(Qt::AlignCenter);
	lockedTextLabel->setWordWrap(true);
	lockedTextLabel->setFixedWidth(EMPTY_STATE_TEXT_WIDTH);
	unlockButton = new QPushButton(page);
	SetButtonRole(*unlockButton, ButtonRole::PRIMARY);

	auto* pageLayout = new QVBoxLayout(page);
	pageLayout->setSpacing(EMPTY_STATE_SPACING);
	pageLayout->addStretch();
	pageLayout->addWidget(illustration, 0, Qt::AlignHCenter);
	pageLayout->addWidget(lockedTitleLabel, 0, Qt::AlignHCenter);
	auto* lockedTextLayout = new QHBoxLayout();
	lockedTextLayout->addStretch();
	lockedTextLayout->addWidget(lockedTextLabel);
	lockedTextLayout->addStretch();
	pageLayout->addLayout(lockedTextLayout);
	pageLayout->addSpacing(EMPTY_STATE_BUTTON_SPACING);
	pageLayout->addWidget(unlockButton, 0, Qt::AlignHCenter);
	pageLayout->addStretch();

	connect(unlockButton, &QPushButton::clicked, this, &MainWindow::Unlock);
	return page;
}

void MainWindow::UnlockIfLocked() {
	if (storageEncryption.Status() == EncryptionStatus::LOCKED) {
		// After the window has painted, so the prompt of the system appears over it.
		QTimer::singleShot(0, this, &MainWindow::Unlock);
	}
}

void MainWindow::Unlock() {
	const std::expected<void, EncryptionFailure> unlocked = storageEncryption.Unlock();
	// A dismissed prompt leaves the lock screen with its button; other failures are explained.
	if (!unlocked.has_value() && unlocked.error().keyStoreFailure != KeyStoreFailure::DISMISSED) {
		QMessageBox::critical(this, tr("Unlock"), EncryptionFailureText(unlocked.error()));
	}
}

QWidget* MainWindow::CreateAccountsPage() {
	auto* page = new QWidget(pageStack);
	accountListView = new AccountListView(steamClock, accountProfiles, page);
	accountListView->setFixedWidth(ACCOUNT_LIST_WIDTH);
	accountDetailsView = new AccountDetailsView(accountProfiles, page);

	auto* pageLayout = new QHBoxLayout(page);
	pageLayout->setContentsMargins(0, 0, 0, 0);
	pageLayout->setSpacing(0);
	pageLayout->addWidget(accountListView);
	pageLayout->addWidget(accountDetailsView, 1);

	connect(accountListView, &AccountListView::RecordSelected, this, &MainWindow::ShowRecord);
	connect(accountListView, &AccountListView::RevealChanged, this, [this](const QUuid& recordId, bool isRevealed) {
		if (accountDetailsView->ShownRecordId() == recordId) {
			accountDetailsView->SetAccountNameHidden(!isRevealed);
		}
	});
	connect(accountDetailsView, &AccountDetailsView::RemoveRequested, this, &MainWindow::ConfirmRemoval);
	connect(accountDetailsView, &AccountDetailsView::AddDataRequested, this, &MainWindow::OpenRecordDataDialog);
	connect(accountDetailsView, &AccountDetailsView::ExportRequested, this, &MainWindow::ExportRecord);
	connect(accountDetailsView, &AccountDetailsView::QrCodeRequested, this, &MainWindow::ShowSteamGuardQr);
	connect(accountDetailsView, &AccountDetailsView::SignInRequested, this, &MainWindow::OpenLoginDialog);
	connect(accountDetailsView, &AccountDetailsView::ApproveSignInRequested, this, &MainWindow::OpenLoginApprovalDialog);
	connect(accountDetailsView, &AccountDetailsView::ConfirmationsRequested, this, &MainWindow::OpenConfirmationsDialog);
	connect(accountDetailsView, &AccountDetailsView::ImportRequested, this, &MainWindow::OpenImportDialog);
	connect(accountDetailsView, &AccountDetailsView::SignOutRequested, this, &MainWindow::ConfirmSignOut);
	connect(accountDetailsView, &AccountDetailsView::SetUpGuardRequested, this, &MainWindow::OpenGuardSetupDialog);
	connect(accountDetailsView, &AccountDetailsView::MoveGuardRequested, this, &MainWindow::OpenGuardMoveLogin);
	connect(accountDetailsView, &AccountDetailsView::AddToGoogleRequested, &cloudSync, &CloudSync::AddToCloud);
	connect(accountDetailsView, &AccountDetailsView::RemoveFromGoogleRequested, this, &MainWindow::ConfirmCloudRemoval);
	connect(accountDetailsView, &AccountDetailsView::CloudConflictRequested, this, &MainWindow::OpenCloudConflict);
	return page;
}

void MainWindow::CreateSystemTray() {
	if (!SystemTray::IsAvailable()) {
		return;
	}
	systemTray = new SystemTray(this);
	connect(systemTray, &SystemTray::OpenRequested, this, &MainWindow::BringToFront);
	connect(systemTray, &SystemTray::QuitRequested, this, &MainWindow::QuitApplication);
	systemTray->Show();
}

void MainWindow::RetranslateUi() {
	topBarTitleLabel->setText(tr("Accounts"));
	lockedTitleLabel->setText(tr("SteamForge is locked"));
	if (storageEncryption.Protection() == KeyProtection::KEYRING_PASSWORD) {
		lockedTextLabel->setText(tr("The data is protected with the password of the SteamForge keyring. To be asked for it every time, untick “Automatically unlock…” in the system window."));
	} else {
		lockedTextLabel->setText(tr("The data is protected with Windows Hello. Unlock it with your PIN, face or fingerprint."));
	}
	unlockButton->setText(tr("Unlock"));
	UpdateStatusButtons();
	addAccountButton->setText(tr("Add account"));
	settingsButton->setText(tr("Settings"));
	emptyStateTitleLabel->setText(tr("No accounts yet"));
	emptyStateTextLabel->setText(tr("Sign in to a Steam account to set up Steam Guard or move it here from your phone, or import an authenticator from Steam Desktop Authenticator or WinAuth."));
	emptyStateSignInButton->setText(tr("Sign in to Steam"));
	emptyStateImportButton->setText(tr("Import from files"));
	signInAction->setText(tr("Sign in to a Steam account…"));
	importAction->setText(tr("Import from files…"));
	loadProblemDetailsButton->setText(tr("Details"));
	if (systemTray != nullptr) {
		systemTray->RetranslateUi();
	}
	UpdateLoadProblems();
	UpdateTimeStatus();
}

void MainWindow::UpdatePages() {
	// Locked, the library is not loaded: nothing may be forgotten or closed for accounts that only look missing.
	if (storageEncryption.Status() == EncryptionStatus::LOCKED) {
		pageStack->setCurrentWidget(lockedPage);
		addAccountButton->setVisible(false);
		return;
	}
	const bool hasRecords = !accountLibrary.Records().empty();
	pageStack->setCurrentWidget(hasRecords ? accountsPage : emptyStatePage);
	addAccountButton->setVisible(hasRecords);
	UpdateLoadProblems();
	CloseConfirmationsOfRemovedRecords();
	ForgetProfilesOfRemovedRecords();
	if (!hasRecords) {
		return;
	}

	accountListView->SetRecords(accountLibrary.Records());
	UpdateConfirmationIndicators();
	UpdateCloudStates();
	const std::optional<QUuid> selectedRecordId = accountListView->SelectedRecordId();
	const QUuid recordId = selectedRecordId.value_or(accountLibrary.Records().front().id);
	accountListView->SelectRecord(recordId);
	ShowRecord(recordId);
}

void MainWindow::UpdateStatusButtons() {
	switch (storageEncryption.Status()) {
		case EncryptionStatus::OFF:
			ShowStatus(*encryptionStatusButton, LOCK_OPEN_ICON_RESOURCE, tr("Not encrypted"), false);
			break;
		case EncryptionStatus::ON:
			ShowStatus(*encryptionStatusButton, LOCK_ICON_RESOURCE, tr("Encrypted"), false);
			break;
		case EncryptionStatus::LOCKED:
			ShowStatus(*encryptionStatusButton, LOCK_ALERT_ICON_RESOURCE, tr("Locked"), true);
			break;
		case EncryptionStatus::KEY_UNAVAILABLE:
			ShowStatus(*encryptionStatusButton, LOCK_ALERT_ICON_RESOURCE, tr("Key unavailable"), true);
			break;
		case EncryptionStatus::UNSUPPORTED:
			ShowStatus(*encryptionStatusButton, LOCK_OPEN_ICON_RESOURCE, tr("Encryption unavailable"), false);
			break;
	}
	encryptionStatusButton->setToolTip(tr("Encryption settings"));
	googleStatusButton->setVisible(true);
	googleStatusButton->setToolTip(googleConnection.Email().isEmpty() ? tr("Google settings") : googleConnection.Email());
	switch (googleConnection.State()) {
		case GoogleConnectionState::UNAVAILABLE:
		case GoogleConnectionState::LOCKED:
			// Without a client there is nothing to offer; locked, the encryption status already says what to do.
			googleStatusButton->setVisible(false);
			break;
		case GoogleConnectionState::DISCONNECTED:
			ShowStatus(*googleStatusButton, CLOUD_OFF_ICON_RESOURCE, tr("Google not connected"), false);
			break;
		case GoogleConnectionState::CONNECTING:
			ShowStatus(*googleStatusButton, CLOUD_ICON_RESOURCE, tr("Connecting to Google…"), false);
			break;
		case GoogleConnectionState::CONNECTED:
			ShowSyncStatus();
			break;
		case GoogleConnectionState::SIGN_IN_REQUIRED:
			ShowStatus(*googleStatusButton, CLOUD_ALERT_ICON_RESOURCE, tr("Sign in to Google again"), true);
			break;
	}
}

void MainWindow::ShowSyncStatus() {
	const QDateTime& syncedAt = cloudSync.LastSyncedAt();
	const QString& email = googleConnection.Email();
	switch (cloudSync.Status()) {
		case CloudSyncStatus::INACTIVE:
			ShowStatus(*googleStatusButton, CLOUD_CHECK_ICON_RESOURCE, tr("Google connected"), false);
			return;
		case CloudSyncStatus::SYNCING:
			// After the first sync the text stays: a pass every few minutes would make the top bar jump.
			ShowStatus(*googleStatusButton, CLOUD_ICON_RESOURCE, syncedAt.isValid() ? tr("Synced") : tr("Syncing…"), false);
			return;
		case CloudSyncStatus::SYNCED:
			ShowStatus(*googleStatusButton, CLOUD_CHECK_ICON_RESOURCE, tr("Synced"), false);
			if (syncedAt.isValid()) {
				googleStatusButton->setToolTip(tr("%1, synced at %2").arg(email, QLocale().toString(syncedAt.time(), QLocale::ShortFormat)));
			}
			return;
		case CloudSyncStatus::NEEDS_DECISION:
			ShowStatus(*googleStatusButton, CLOUD_ALERT_ICON_RESOURCE, tr("Choose a version"), true);
			googleStatusButton->setToolTip(tr("Google has another version of an account: choose which one to keep"));
			return;
		case CloudSyncStatus::UNREACHABLE:
			ShowStatus(*googleStatusButton, CLOUD_OFF_ICON_RESOURCE, tr("No connection to Google"), false);
			googleStatusButton->setToolTip(tr("SteamForge tries again in a few minutes"));
			return;
		case CloudSyncStatus::NOT_SAVED:
			ShowStatus(*googleStatusButton, CLOUD_ALERT_ICON_RESOURCE, tr("Not synced"), true);
			googleStatusButton->setToolTip(tr("A change could not be saved on this computer. SteamForge tries again at the next sync"));
			return;
		case CloudSyncStatus::UNREADABLE_IN_CLOUD:
			ShowStatus(*googleStatusButton, CLOUD_ALERT_ICON_RESOURCE, tr("Not synced"), true);
			googleStatusButton->setToolTip(tr("An account in Google could not be read. The accounts on this computer stay as they are"));
			return;
	}
}

void MainWindow::OpenGoogleStatus() {
	// A version to choose is the one thing that waits for the user.
	if (const std::vector<CloudConflict>& conflicts = cloudSync.Conflicts(); googleConnection.State() == GoogleConnectionState::CONNECTED && !conflicts.empty()) {
		OpenCloudConflict(conflicts.front().localRecordId);
		return;
	}
	OpenSettingsDialog(SettingsSection::GOOGLE);
}

void MainWindow::UpdateCloudStates() {
	for (const AuthenticatorRecord& record : accountLibrary.Records()) {
		accountListView->SetCloudState(record.id, CloudStateOf(record));
	}
	if (const std::optional<QUuid> shownRecordId = accountDetailsView->ShownRecordId(); shownRecordId.has_value()) {
		accountDetailsView->SetCloudInfo(CloudInfoOf(*shownRecordId));
	}
	UpdateStatusButtons();
	NotifyCloudArrivals();
}

RowCloudState MainWindow::CloudStateOf(const AuthenticatorRecord& record) const {
	if (googleConnection.State() != GoogleConnectionState::CONNECTED || !DetermineCapabilities(record).canGenerateCodes) {
		return RowCloudState::NONE;
	}
	if (cloudSync.HasConflict(record.id)) {
		return RowCloudState::CONFLICT;
	}
	return cloudSync.IsInCloud(record.id) ? RowCloudState::IN_CLOUD : RowCloudState::LOCAL_ONLY;
}

RecordCloudInfo MainWindow::CloudInfoOf(const QUuid& recordId) const {
	return RecordCloudInfo{
		.isGoogleConnected = googleConnection.State() == GoogleConnectionState::CONNECTED,
		.isInCloud = cloudSync.IsInCloud(recordId),
		.hasConflict = cloudSync.HasConflict(recordId),
		.isChanging = cloudSync.IsChanging(recordId),
		.lastSyncedAt = cloudSync.LastSyncedAt(),
	};
}

void MainWindow::SyncIfStale() {
	if (googleConnection.State() != GoogleConnectionState::CONNECTED || cloudSync.Status() == CloudSyncStatus::SYNCING) {
		return;
	}
	if (const QDateTime& syncedAt = cloudSync.LastSyncedAt(); !syncedAt.isValid() || syncedAt.secsTo(QDateTime::currentDateTime()) >= ACTIVATION_SYNC_AGE_SECONDS) {
		cloudSync.Synchronize();
	}
}

void MainWindow::NotifyCloudArrivals() {
	if (arrivedRecordIds.empty() || cloudSync.Status() == CloudSyncStatus::SYNCING) {
		return;
	}
	QStringList titles;
	for (const QUuid& recordId : std::exchange(arrivedRecordIds, {})) {
		if (const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId); record != nullptr) {
			titles.append(AccountTitleFor(*record));
		}
	}
	if (!titles.isEmpty()) {
		Notify(tr("Accounts from Google: %n", nullptr, static_cast<int>(titles.size())), titles.join(QStringLiteral(", ")));
	}
}

void MainWindow::ConfirmCloudRemoval(const QUuid& recordId) {
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (record == nullptr || !cloudSync.IsInCloud(recordId)) {
		return;
	}
	QMessageBox confirmation(QMessageBox::Question, tr("Remove from Google"), tr("Remove %1 from Google?").arg(AccountTitleFor(*record)), QMessageBox::NoButton, DialogParent());
	confirmation.setInformativeText(BulletLines({
		tr("The copy in Google is deleted."),
		tr("On this computer the account stays, and its codes and confirmations keep working."),
		tr("On your other computers the account stays too, but no longer syncs."),
		tr("Steam Guard stays on the Steam account."),
	}));
	QPushButton* removeButton = confirmation.addButton(tr("Remove from Google"), QMessageBox::DestructiveRole);
	SetButtonRole(*removeButton, ButtonRole::PRIMARY);
	confirmation.setDefaultButton(confirmation.addButton(QMessageBox::Cancel));
	confirmation.exec();
	if (confirmation.clickedButton() == removeButton) {
		cloudSync.RemoveFromCloud(recordId);
	}
}

void MainWindow::OpenCloudConflict(const QUuid& recordId) {
	const std::vector<CloudConflict>& conflicts = cloudSync.Conflicts();
	const auto found = std::ranges::find(conflicts, recordId, &CloudConflict::localRecordId);
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (found == conflicts.end() || record == nullptr) {
		return;
	}
	// The dialog keeps its own copy: a pass may change the list while it is open.
	CloudConflictDialog conflictDialog(*found, *record, AccountTitleFor(*record), steamClock, DialogParent());
	if (conflictDialog.exec() != QDialog::Accepted) {
		return;
	}
	if (const std::optional<ConflictChoice> choice = conflictDialog.Choice(); choice.has_value()) {
		cloudSync.ResolveConflict(conflictDialog.CloudRecordId(), *choice);
	}
}

QWidget* MainWindow::DialogParent() {
	if (QApplication::activeModalWidget() != nullptr) {
		return QApplication::activeModalWidget();
	}
	return this;
}

void MainWindow::OpenEncryptionStatus() {
	// Locked, the one useful step is to unlock; the settings would only say the same.
	if (storageEncryption.Status() == EncryptionStatus::LOCKED) {
		Unlock();
		return;
	}
	OpenSettingsDialog(SettingsSection::ENCRYPTION);
}

bool MainWindow::OfferEncryption() {
	const KeyProtection preferredProtection = storageEncryption.PreferredProtection();
	QString protectionFact = tr("The keyring of the system keeps the key, so no password is asked.");
	if (preferredProtection == KeyProtection::WINDOWS_HELLO) {
		protectionFact = tr("Windows Hello guards the key: SteamForge asks for your PIN, face or fingerprint at every start.");
	} else if (preferredProtection == KeyProtection::KEYRING_PASSWORD) {
		protectionFact = tr("The key goes to a separate SteamForge keyring. The system asks you to choose its password now and asks for it at every start.");
	}
	QStringList facts{protectionFact};
	if (preferredProtection == KeyProtection::KEYRING_PASSWORD) {
		facts.append(tr("In the system window, untick “Automatically unlock…”, otherwise the password is asked only once."));
	}
	facts.append({
		tr("Protects the data when the folder is copied, backed up or synced to a cloud."),
		tr("Does not protect from programs running under your user account."),
		tr("A data folder moved to another computer or user account does not open there: move accounts with the export to .maFile."),
	});
	QStringList factLines;
	for (const QString& fact : facts) {
		factLines.append(QStringLiteral("• ") + fact);
	}
	QMessageBox offer(QMessageBox::Question, tr("Turn on encryption"), tr("Encrypt the authenticators and Steam sign-ins on this computer?"), QMessageBox::NoButton, this);
	offer.setInformativeText(factLines.join(QLatin1Char('\n')));
	QPushButton* encryptButton = offer.addButton(tr("Encrypt"), QMessageBox::AcceptRole);
	SetButtonRole(*encryptButton, ButtonRole::PRIMARY);
	offer.setDefaultButton(offer.addButton(QMessageBox::Cancel));
	offer.exec();
	if (offer.clickedButton() != encryptButton) {
		return false;
	}
	std::expected<void, EncryptionFailure> enabled = storageEncryption.Enable(preferredProtection);
	if (!enabled.has_value() && enabled.error().keyStoreFailure == KeyStoreFailure::NOT_SET_UP && preferredProtection != KeyProtection::USER_ACCOUNT) {
		if (!OfferEncryptionWithoutPrompt(preferredProtection)) {
			return false;
		}
		enabled = storageEncryption.Enable(KeyProtection::USER_ACCOUNT);
	}
	if (!enabled.has_value()) {
		if (enabled.error().keyStoreFailure != KeyStoreFailure::DISMISSED) {
			QMessageBox::critical(this, tr("Turn on encryption"), EncryptionFailureText(enabled.error()));
		}
		return false;
	}
	Notify(tr("Encryption is on"), tr("The authenticators and Steam sign-ins on this computer are stored encrypted."));
	return true;
}

bool MainWindow::OfferEncryptionWithoutPrompt(KeyProtection missingProtection) {
	const bool isWindowsHello = missingProtection == KeyProtection::WINDOWS_HELLO;
	QMessageBox fallback(QMessageBox::Question, tr("Turn on encryption"), MissingProtectionText(missingProtection), QMessageBox::NoButton, this);
	if (isWindowsHello) {
		fallback.setInformativeText(tr("Set up a PIN in Windows Settings, Accounts, Sign-in options, and try again. Or turn on encryption without Windows Hello: Windows then keeps the key for your user account and asks nothing at the start."));
	} else {
		fallback.setInformativeText(tr("Encryption can use the main keyring instead. It opens together with your sign-in to the system, so no password is asked at the start."));
	}
	const QPushButton* fallbackButton = fallback.addButton(isWindowsHello ? tr("Encrypt without Windows Hello") : tr("Encrypt with the main keyring"), QMessageBox::AcceptRole);
	fallback.setDefaultButton(fallback.addButton(QMessageBox::Cancel));
	fallback.exec();
	return fallback.clickedButton() == fallbackButton;
}

void MainWindow::TurnOffEncryption() {
	// The key of the Google cloud copy is kept under the local key: turned off, it would lie on the disk in the clear.
	if (googleConnection.HasSavedConnection()) {
		QMessageBox::information(this, tr("Turn off encryption"), tr("The key of your Google cloud copy is kept under this encryption, so it stays on while Google is connected. To turn it off, disconnect Google in the settings first."));
		return;
	}
	QMessageBox confirmation(QMessageBox::Question, tr("Turn off encryption"), tr("Store the data on this computer without encryption?"), QMessageBox::NoButton, this);
	confirmation.setInformativeText(tr("The authenticators and Steam sign-ins are then kept in the data folder unencrypted, as before encryption was turned on."));
	const QPushButton* turnOffButton = confirmation.addButton(tr("Turn off encryption"), QMessageBox::DestructiveRole);
	confirmation.setDefaultButton(confirmation.addButton(QMessageBox::Cancel));
	confirmation.exec();
	if (confirmation.clickedButton() != turnOffButton) {
		return;
	}
	const std::expected<void, EncryptionFailure> disabled = storageEncryption.Disable();
	if (!disabled.has_value()) {
		if (disabled.error().keyStoreFailure != KeyStoreFailure::DISMISSED) {
			QMessageBox::critical(this, tr("Turn off encryption"), EncryptionFailureText(disabled.error()));
		}
		return;
	}
	Notify(tr("Encryption is off"), tr("The data on this computer is stored without encryption."));
}

QString MainWindow::MissingProtectionText(KeyProtection protection) {
	if (protection == KeyProtection::KEYRING_PASSWORD) {
		return tr("The keyring of the system cannot make a separate keyring that stays locked.");
	}
	return tr("Windows Hello is not set up on this computer.");
}

QString MainWindow::EncryptionFailureText(const EncryptionFailure& failure) const {
	if (failure.unreadableFile.has_value()) {
		return tr("Not every file opens, and this change would leave it unreadable for good: %1 Nothing has changed.").arg(StorageErrorText(*failure.unreadableFile));
	}
	if (failure.storageError.has_value()) {
		return tr("Not every file could be rewritten: %1 The change continues at the next start.").arg(StorageErrorText(*failure.storageError));
	}
	switch (failure.keyStoreFailure.value_or(KeyStoreFailure::FAILED)) {
		case KeyStoreFailure::UNAVAILABLE:
			return tr("This system has nowhere to keep the encryption key.");
		case KeyStoreFailure::DISMISSED:
			return tr("The confirmation was cancelled. Nothing has changed.");
		case KeyStoreFailure::NOT_SET_UP:
			return MissingProtectionText(storageEncryption.PreferredProtection());
		case KeyStoreFailure::NOT_FOUND:
		case KeyStoreFailure::UNREADABLE:
			return tr("The encryption key could not be read.");
		case KeyStoreFailure::FAILED:
			break;
	}
	return tr("The system did not keep the encryption key. Nothing has changed.");
}

void MainWindow::ForgetProfilesOfRemovedRecords() {
	QSet<quint64> steamIds;
	for (const AuthenticatorRecord& record : accountLibrary.Records()) {
		if (record.account.steamId.has_value()) {
			steamIds.insert(*record.account.steamId);
		}
	}
	accountProfiles.KeepOnly(steamIds);
}

void MainWindow::UpdateLoadProblems() {
	const auto problemCount = static_cast<int>(accountLibrary.LoadProblems().size());
	loadProblemBanner->setVisible(problemCount > 0);
	loadProblemLabel->setText(tr("Account files that could not be read: %n. They were skipped and left unchanged.", nullptr, problemCount));
}

void MainWindow::UpdateTimeStatus() {
	switch (steamClock.State()) {
		case SteamClockState::NOT_SYNCHRONIZED:
			timeStatusLabel->setText(tr("Checking the time with Steam…"));
			break;
		case SteamClockState::SYNCHRONIZED: {
			// The offset is Steam time minus local time: positive when this computer's clock is behind.
			const auto offsetSeconds = static_cast<qint64>(std::chrono::round<std::chrono::seconds>(steamClock.Offset()).count());
			if (std::abs(offsetSeconds) < NOTICEABLE_CLOCK_OFFSET_SECONDS) {
				timeStatusLabel->setText(tr("Time is in sync with Steam."));
			} else if (offsetSeconds > 0) {
				timeStatusLabel->setText(tr("Time is in sync with Steam. This computer's clock is %1 s behind, and the codes allow for it.").arg(offsetSeconds));
			} else {
				timeStatusLabel->setText(tr("Time is in sync with Steam. This computer's clock is %1 s ahead, and the codes allow for it.").arg(-offsetSeconds));
			}
			break;
		}
		case SteamClockState::SYNCHRONIZATION_FAILED:
			timeStatusLabel->setText(tr("Could not check the time with Steam, so codes use the clock of this computer for now. We'll try again automatically."));
			break;
	}
}

void MainWindow::ShowRecord(const QUuid& recordId) {
	if (const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId); record != nullptr) {
		accountDetailsView->SetAccountNameHidden(!accountListView->IsRevealed(recordId));
		accountDetailsView->ShowRecord(*record, accountLibrary.FindSession(recordId), confirmationMonitor.Find(recordId));
		accountDetailsView->SetCloudInfo(CloudInfoOf(recordId));
	}
}

void MainWindow::RefreshShownRecord(const QUuid& recordId) {
	if (accountDetailsView->ShownRecordId() == recordId) {
		ShowRecord(recordId);
	}
}

void MainWindow::ShowAddAccountMenu() {
	addAccountMenu->exec(addAccountButton->mapToGlobal(QPoint(0, addAccountButton->height())));
}

void MainWindow::OpenNewAccountLogin() {
	AuthenticatorRecord newAccount;
	newAccount.id = QUuid::createUuid();
	RunLoginDialog(newAccount, LoginPurpose::SIGN_IN);
}

void MainWindow::OpenLoginDialog(const QUuid& recordId) {
	if (const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId); record != nullptr) {
		RunLoginDialog(*record, LoginPurpose::SIGN_IN);
	}
}

void MainWindow::OpenGuardMoveLogin(const QUuid& recordId) {
	if (const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId); record != nullptr) {
		RunLoginDialog(*record, LoginPurpose::MOVE_GUARD);
	}
}

void MainWindow::RunLoginDialog(const AuthenticatorRecord& loginRecord, LoginPurpose purpose) {
	LoginDialog loginDialog(accountLibrary, steamClock, storageEncryption, loginRecord, AccountTitleFor(loginRecord), purpose, this);
	connect(&loginDialog, &LoginDialog::EncryptionRequested, this, &MainWindow::OfferEncryption);
	// A Steam Guard that is in Google already goes there by itself after the move.
	if (purpose == LoginPurpose::MOVE_GUARD && googleConnection.State() == GoogleConnectionState::CONNECTED && !cloudSync.IsInCloud(loginRecord.id)) {
		loginDialog.OfferGoogle();
	}
	const int result = loginDialog.exec();
	// The move is saved even when the sign-in after it is cancelled.
	if (loginDialog.AddsToGoogle()) {
		cloudSync.AddToCloud(loginRecord.id);
	}
	if (result != QDialog::Accepted) {
		return;
	}
	if (const std::optional<QUuid> signedInRecordId = loginDialog.SignedInRecordId(); signedInRecordId.has_value()) {
		accountListView->SelectRecord(*signedInRecordId);
		ShowRecord(*signedInRecordId);
	}
}

void MainWindow::OpenGuardSetupDialog(const QUuid& recordId) {
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	const std::optional<AccountSession> session = accountLibrary.FindSession(recordId);
	if (record == nullptr || !session.has_value()) {
		return;
	}
	GuardSetupDialog guardSetupDialog(accountLibrary, steamClock, *record, AccountTitleFor(*record), *session, this);
	if (googleConnection.State() == GoogleConnectionState::CONNECTED) {
		guardSetupDialog.OfferGoogle();
	}
	guardSetupDialog.exec();
	if (guardSetupDialog.AddsToGoogle()) {
		cloudSync.AddToCloud(recordId);
	}
}

void MainWindow::OpenLoginApprovalDialog(const QUuid& recordId) {
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	const std::optional<AccountSession> session = accountLibrary.FindSession(recordId);
	if (record == nullptr || !session.has_value()) {
		return;
	}
	// Opened without a nested event loop: the dialog hides itself while it takes the screenshot.
	auto* loginApprovalDialog = new LoginApprovalDialog(accountLibrary, *record, AccountTitleFor(*record), *session, this);
	loginApprovalDialog->setAttribute(Qt::WA_DeleteOnClose);
	// Queued, so the sign-in opens after the approval dialog has finished closing.
	connect(loginApprovalDialog, &LoginApprovalDialog::SignInAgainRequested, this, &MainWindow::OpenLoginDialog, Qt::QueuedConnection);
	loginApprovalDialog->open();
}

void MainWindow::OpenConfirmationsDialog(const QUuid& recordId) {
	if (const QPointer<ConfirmationsDialog> openDialog = confirmationsDialogs.value(recordId); openDialog != nullptr) {
		openDialog->show();
		openDialog->raise();
		openDialog->activateWindow();
		return;
	}
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (record == nullptr) {
		return;
	}
	// Not modal: confirmations of several accounts can stay open side by side, each fixed to its account.
	auto* confirmationsDialog = new ConfirmationsDialog(confirmationMonitor, steamImageCache, *record, AccountTitleFor(*record), accountProfiles.Profile(record->account.steamId).avatar, this);
	confirmationsDialog->setAttribute(Qt::WA_DeleteOnClose);
	// Queued, so the sign-in opens after the click in the confirmations window has finished.
	connect(confirmationsDialog, &ConfirmationsDialog::SignInRequested, this, &MainWindow::OpenLoginDialog, Qt::QueuedConnection);
	confirmationsDialogs.insert(recordId, confirmationsDialog);
	confirmationsDialog->show();
}

void MainWindow::OpenConfirmationsFromNotification(const QUuid& recordId) {
	// A notification about the program rather than an account only shows the window.
	if (recordId.isNull()) {
		BringToFront();
		return;
	}
	if (accountLibrary.FindRecord(recordId) == nullptr) {
		return;
	}
	BringToFront();
	accountListView->SelectRecord(recordId);
	if (confirmationMonitor.Find(recordId) != nullptr) {
		OpenConfirmationsDialog(recordId);
	}
}

void MainWindow::ConnectConfirmations() {
	for (const AuthenticatorRecord& record : accountLibrary.Records()) {
		if (const AccountConfirmations* confirmations = confirmationMonitor.Find(record.id); confirmations != nullptr) {
			connect(confirmations, &AccountConfirmations::Changed, this, &MainWindow::UpdateConfirmationIndicators, Qt::UniqueConnection);
		}
	}
	UpdateConfirmationIndicators();
}

void MainWindow::UpdateConfirmationIndicators() {
	for (const AuthenticatorRecord& record : accountLibrary.Records()) {
		const AccountConfirmations* confirmations = confirmationMonitor.Find(record.id);
		const int waitingCount = confirmations != nullptr ? static_cast<int>(confirmations->Confirmations().size()) : 0;
		accountListView->SetConfirmationCount(record.id, waitingCount);
	}
	if (const std::optional<QUuid> shownRecordId = accountDetailsView->ShownRecordId(); shownRecordId.has_value()) {
		ShowRecord(*shownRecordId);
	}
}

void MainWindow::NotifyConfirmations(const QUuid& recordId, const std::vector<PendingConfirmation>& newConfirmations) {
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (record == nullptr || newConfirmations.empty()) {
		return;
	}
	if (const QPointer<ConfirmationsDialog> openDialog = confirmationsDialogs.value(recordId); openDialog != nullptr && openDialog->isActiveWindow()) {
		return;
	}
	const auto newCount = static_cast<int>(newConfirmations.size());
	QStringList lines;
	for (const PendingConfirmation& confirmation : newConfirmations | std::views::take(NOTIFIED_CONFIRMATION_LIMIT)) {
		lines.append(ConfirmationLine(confirmation));
	}
	if (newConfirmations.size() > NOTIFIED_CONFIRMATION_LIMIT) {
		lines.append(tr("and %n more", nullptr, newCount - static_cast<int>(NOTIFIED_CONFIRMATION_LIMIT)));
	}
	desktopNotifications->Show(tr("%n new confirmations for %1", nullptr, newCount).arg(AccountTitleFor(*record)), lines.join(QLatin1Char('\n')), recordId);
}

// The Steam name once Steam has told it, so the account name stays out of sight; notifications show over other windows too.
QString MainWindow::AccountTitleFor(const AuthenticatorRecord& record) {
	return AccountTitle(record, accountProfiles.Profile(record.account.steamId).personaName);
}

void MainWindow::NotifySignInRequired(const QUuid& recordId) {
	if (const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId); record != nullptr) {
		desktopNotifications->Show(tr("Sign in again: %1").arg(AccountTitleFor(*record)), tr("Steam no longer accepts the saved sign-in. Sign in again to keep getting confirmations."), recordId);
	}
}

void MainWindow::CloseConfirmationsOfRemovedRecords() {
	for (auto dialogIterator = confirmationsDialogs.begin(); dialogIterator != confirmationsDialogs.end();) {
		if (dialogIterator.value() != nullptr && accountLibrary.FindRecord(dialogIterator.key()) != nullptr) {
			++dialogIterator;
			continue;
		}
		if (dialogIterator.value() != nullptr) {
			dialogIterator.value()->close();
		}
		dialogIterator = confirmationsDialogs.erase(dialogIterator);
	}
}

void MainWindow::ConfirmSignOut(const QUuid& recordId) {
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (record == nullptr) {
		return;
	}
	QMessageBox confirmation(QMessageBox::Question, tr("Sign out of Steam"), tr("Remove the saved Steam sign-in of %1 from this computer?").arg(AccountTitleFor(*record)), QMessageBox::NoButton, this);
	confirmation.setInformativeText(tr("Confirmations, trades and approving sign-ins need a new sign-in afterwards. Steam Guard codes keep working."));
	const QPushButton* signOutButton = confirmation.addButton(tr("Sign out"), QMessageBox::AcceptRole);
	confirmation.setDefaultButton(confirmation.addButton(QMessageBox::Cancel));
	confirmation.exec();
	if (confirmation.clickedButton() != signOutButton) {
		return;
	}
	if (const std::expected<void, StorageError> removed = accountLibrary.RemoveSession(recordId); !removed.has_value()) {
		QMessageBox::critical(this, tr("Sign out of Steam"), StorageErrorText(removed.error()));
	}
}

void MainWindow::OpenImportDialog() {
	RunImportDialog({});
}

void MainWindow::RunImportDialog(const QStringList& filePaths) {
	ImportDialog importDialog(accountLibrary, this);
	if (!filePaths.isEmpty()) {
		importDialog.AddFiles(filePaths);
	}
	if (googleConnection.State() == GoogleConnectionState::CONNECTED) {
		importDialog.OfferGoogle();
	}
	if (importDialog.exec() != QDialog::Accepted || importDialog.ImportedRecordIds().empty()) {
		return;
	}
	const std::vector<QUuid>& importedRecordIds = importDialog.ImportedRecordIds();
	if (importDialog.AddsToGoogle()) {
		for (const QUuid& recordId : importedRecordIds) {
			cloudSync.AddToCloud(recordId);
		}
	}
	accountListView->SelectRecord(importedRecordIds.front());
	const bool isEveryRecordReady = std::ranges::all_of(importedRecordIds, [this](const QUuid& recordId) {
		const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
		return record != nullptr && DetermineCapabilities(*record).canSignConfirmations && accountLibrary.FindSession(recordId).has_value();
	});
	const auto importedCount = static_cast<int>(importedRecordIds.size());
	const QString nextStep = isEveryRecordReady ? tr("Codes and confirmations work already.") : tr("The panel on the right shows what is left to set up.");
	Notify(tr("Accounts added: %n", nullptr, importedCount), nextStep);
}

void MainWindow::Notify(const QString& title, const QString& text) {
	desktopNotifications->Show(title, text, QUuid());
}

void MainWindow::ShowSteamGuardQr(const QUuid& recordId) {
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (record == nullptr || !DetermineCapabilities(*record).canGenerateCodes) {
		return;
	}
	const SteamGuardLinks links{
		.steamEncoderLink = SteamGuardAppUri(*record, SteamGuardLinkStyle::STEAM_ENCODER),
		.steamTypeLink = SteamGuardAppUri(*record, SteamGuardLinkStyle::STEAM_TYPE),
	};
	SteamGuardQrDialog qrDialog(AccountTitleFor(*record), links, this);
	qrDialog.exec();
}

void MainWindow::ExportRecord(const QUuid& recordId) {
	const AuthenticatorRecord* foundRecord = accountLibrary.FindRecord(recordId);
	if (foundRecord == nullptr) {
		return;
	}
	// A copy: sync goes on while the questions are open and may move the records of the library.
	const AuthenticatorRecord record = *foundRecord;
	const RecordCapabilities capabilities = DetermineCapabilities(record);
	QStringList contents{tr("Steam Guard codes")};
	if (capabilities.canSignConfirmations) {
		contents.append(tr("trade and market confirmations"));
	}
	if (capabilities.hasRevocationCode) {
		contents.append(tr("the recovery code"));
	}
	QMessageBox warning(QMessageBox::Warning, tr("Export to .maFile"), tr("The file holds the secrets of %1 without encryption: with it anyone can get its Steam Guard codes and confirm its trades. Keep it as safely as a password.").arg(AccountTitleFor(record)), QMessageBox::Cancel, this);
	warning.setInformativeText(tr("The file contains: %1. The Steam sign-in is not exported: Steam Desktop Authenticator asks to sign in when it imports the file.").arg(contents.join(QStringLiteral(", "))));
	const QPushButton* saveButton = warning.addButton(tr("Choose where to save…"), QMessageBox::AcceptRole);
	warning.setDefaultButton(QMessageBox::Cancel);
	warning.exec();
	if (warning.clickedButton() != saveButton) {
		return;
	}
	const QString suggestedPath = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(MaFileName(record));
	const QString filePath = QFileDialog::getSaveFileName(this, tr("Export to .maFile"), suggestedPath, tr("Steam Desktop Authenticator files (*.maFile)"));
	if (filePath.isEmpty()) {
		return;
	}
	if (const std::expected<void, StorageError> written = WriteOwnerOnlyFile(filePath, ExportMaFile(record)); !written.has_value()) {
		QMessageBox::critical(this, tr("Export to .maFile"), StorageErrorText(written.error()));
		return;
	}
	QMessageBox::information(this, tr("Export to .maFile"), tr("The authenticator of %1 is saved to %2.").arg(AccountTitleFor(record), QDir::toNativeSeparators(filePath)));
}

void MainWindow::OpenSettingsDialog(SettingsSection section) {
	const SettingsDialog::CloudAccountsSource cloudAccounts{
		.sync = cloudSync,
		.library = accountLibrary,
		.titleProvider =
			[this](const AuthenticatorRecord& record) {
				return AccountTitleFor(record);
			},
	};
	SettingsDialog settingsDialog(settings.Language(), settings.ChecksForUpdates(), dataLocation, storageEncryption, googleConnection, cloudAccounts, section, this);
	const CloudAccountsView& cloudAccountsView = settingsDialog.CloudAccounts();
	connect(&cloudAccountsView, &CloudAccountsView::AddRequested, &cloudSync, &CloudSync::AddToCloud);
	connect(&cloudAccountsView, &CloudAccountsView::RemoveRequested, this, &MainWindow::ConfirmCloudRemoval);
	connect(&cloudAccountsView, &CloudAccountsView::ConflictRequested, this, &MainWindow::OpenCloudConflict);
	connect(&cloudAccountsView, &CloudAccountsView::ReturnRequested, &cloudSync, &CloudSync::ReturnFromCloud);
	connect(&cloudAccountsView, &CloudAccountsView::SyncRequested, &cloudSync, &CloudSync::Synchronize);
	connect(&settingsDialog, &SettingsDialog::LanguageSelected, this, &MainWindow::LanguageChangeRequested);
	connect(&settingsDialog, &SettingsDialog::UpdateChecksToggled, this, &MainWindow::UpdateChecksChangeRequested);
	connect(&settingsDialog, &SettingsDialog::GoogleConnectRequested, this, &MainWindow::OpenGoogleConnectDialog);
	connect(&settingsDialog, &SettingsDialog::GoogleDisconnectRequested, this, &MainWindow::ConfirmGoogleDisconnect);
	connect(&settingsDialog, &SettingsDialog::EncryptionTurnOnRequested, this, &MainWindow::OfferEncryption);
	connect(&settingsDialog, &SettingsDialog::EncryptionTurnOffRequested, this, &MainWindow::TurnOffEncryption);
	connect(&settingsDialog, &SettingsDialog::UnlockRequested, this, &MainWindow::Unlock);
	settingsDialog.exec();
}

void MainWindow::OpenGoogleConnectDialog() {
	switch (storageEncryption.Status()) {
		case EncryptionStatus::OFF:
		case EncryptionStatus::ON:
			break;
		case EncryptionStatus::LOCKED:
			Unlock();
			if (storageEncryption.Status() != EncryptionStatus::ON) {
				return;
			}
			break;
		case EncryptionStatus::KEY_UNAVAILABLE:
		case EncryptionStatus::UNSUPPORTED:
			QMessageBox::information(this, tr("Connect Google"), tr("Google needs encryption on this computer, and encryption is not available now. The Encryption section of the settings tells why."));
			return;
	}
	const GoogleConnectDialog::EncryptionStarter turnOnEncryption = [this]() {
		return OfferEncryption();
	};
	const GoogleConnectDialog::AccountTitleProvider titleProvider = [this](const AuthenticatorRecord& record) {
		return AccountTitleFor(record);
	};
	GoogleConnectDialog connectDialog(googleConnection, cloudSync, accountLibrary, titleProvider, turnOnEncryption, storageEncryption.Status() == EncryptionStatus::ON, this);
	connectDialog.exec();
}

void MainWindow::ConfirmGoogleDisconnect() {
	QMessageBox confirmation(QMessageBox::Question, tr("Disconnect Google"), tr("Disconnect Google on this computer?"), QMessageBox::NoButton, this);
	confirmation.setInformativeText(tr("The cloud copy stays in your Google account, and other computers keep working with it. The accounts on this computer stay as they are. To connect this computer again, you need the recovery key."));
	QPushButton* disconnectButton = confirmation.addButton(tr("Disconnect"), QMessageBox::AcceptRole);
	SetButtonRole(*disconnectButton, ButtonRole::PRIMARY);
	confirmation.setDefaultButton(confirmation.addButton(QMessageBox::Cancel));
	confirmation.exec();
	if (confirmation.clickedButton() != disconnectButton) {
		return;
	}
	if (const std::expected<void, StorageError> disconnected = googleConnection.Disconnect(); !disconnected.has_value()) {
		QMessageBox::critical(this, tr("Disconnect Google"), StorageErrorText(disconnected.error()));
		return;
	}
	Notify(tr("Google is disconnected"), tr("This computer no longer syncs with Google. The cloud copy stays in your Google account."));
}

void MainWindow::ConfirmRemoval(const QUuid& recordId) {
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (record == nullptr) {
		return;
	}
	const RecordCapabilities capabilities = DetermineCapabilities(*record);
	QStringList consequences{
		tr("The file with the secrets is deleted from this computer, and the codes and confirmations of this account no longer appear here."),
		tr("Steam Guard stays on the Steam account: removing the record here does not turn it off."),
	};
	if (accountLibrary.FindSession(recordId).has_value()) {
		consequences.append(tr("The saved Steam sign-in is removed from this computer as well."));
	}
	const bool isInCloud = cloudSync.IsInCloud(recordId);
	// Removing from Google needs the connection; otherwise only the choice to keep the copy there is left.
	const bool canRemoveFromCloud = isInCloud && googleConnection.State() == GoogleConnectionState::CONNECTED;
	if (isInCloud) {
		consequences.append(tr("The copy in Google stays for your other computers unless you tick the box below. The account does not come back here by itself; you can return it in the settings, in the Google section."));
	}
	consequences.append(capabilities.hasRevocationCode ? tr("Without another copy of the secrets or the recovery code you can lose access to the account.") : tr("No recovery code is saved: without another copy of the secrets you can lose access to the account."));
	QString consequenceText = BulletLines(consequences);
	if (capabilities.canGenerateCodes) {
		consequenceText += QStringLiteral("\n\n") + tr("Recommended: export the authenticator to a .maFile first and keep it as safely as a password.");
	}

	const QString accountTitle = AccountTitleFor(*record);
	QMessageBox confirmation(QMessageBox::Warning, tr("Remove account"), tr("Remove %1 from SteamForge?").arg(accountTitle), QMessageBox::NoButton, this);
	confirmation.setInformativeText(consequenceText);
	QCheckBox* cloudRemovalCheckBox = nullptr;
	if (canRemoveFromCloud) {
		cloudRemovalCheckBox = new QCheckBox(tr("Also remove it from Google"), &confirmation);
		confirmation.setCheckBox(cloudRemovalCheckBox);
	}
	const QPushButton* exportButton = capabilities.canGenerateCodes ? confirmation.addButton(tr("Export first…"), QMessageBox::ActionRole) : nullptr;
	QPushButton* removeButton = confirmation.addButton(tr("Remove"), QMessageBox::DestructiveRole);
	SetButtonRole(*removeButton, ButtonRole::PRIMARY);
	confirmation.setDefaultButton(confirmation.addButton(QMessageBox::Cancel));
	confirmation.exec();
	if (exportButton != nullptr && confirmation.clickedButton() == exportButton) {
		ExportRecord(recordId);
		return;
	}
	if (confirmation.clickedButton() != removeButton) {
		return;
	}

	if (const std::expected<void, StorageError> removed = accountLibrary.Remove(recordId); !removed.has_value()) {
		QMessageBox::critical(this, tr("Remove account"), StorageErrorText(removed.error()));
		return;
	}
	if (!isInCloud) {
		return;
	}
	if (cloudRemovalCheckBox != nullptr && cloudRemovalCheckBox->isChecked()) {
		cloudSync.RemoveFromCloud(recordId);
	}
	// Until the copy in Google is gone it counts as left there, so a removal that fails cannot bring it back here.
	cloudSync.LeaveInCloud(recordId, accountTitle);
}

void MainWindow::OpenRecordDataDialog(const QUuid& recordId) {
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (record == nullptr) {
		return;
	}
	RecordDataDialog recordDataDialog(accountLibrary, *record, AccountTitleFor(*record), accountLibrary.FindSession(recordId), this);
	recordDataDialog.exec();
}

void MainWindow::ShowLoadProblemDetails() {
	QStringList problemTexts;
	for (const StorageError& problem : accountLibrary.LoadProblems()) {
		problemTexts.append(StorageErrorText(problem));
	}
	QMessageBox details(QMessageBox::Warning, tr("Unreadable account files"), tr("These files were skipped and left unchanged in %1:").arg(QDir::toNativeSeparators(accountLibrary.StorageDirectory())), QMessageBox::Ok, this);
	details.setInformativeText(problemTexts.join(QLatin1Char('\n')));
	details.exec();
}

} // namespace SteamForge
