#include "ui/account_details_view.h"

#include "app/account_profiles.h"
#include "steam/jwt_claims.h"
#include "ui/illustration_view.h"
#include "ui/record_presentation.h"
#include "ui/steam_picture.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int HEADER_AVATAR_EDGE = 64;
	constexpr int HEADER_HORIZONTAL_SPACING = 16;
	constexpr int HEADER_VERTICAL_SPACING = 4;
	constexpr QLatin1StringView MANAGE_ICON_RESOURCE{":/styles/gear.svg"};
	constexpr QSize MANAGE_ICON_SIZE{18, 18};
	constexpr QMargins PAGE_MARGINS{32, 28, 32, 24};
	constexpr QMargins CARD_MARGINS{24, 20, 24, 20};
	constexpr int PAGE_SPACING = 16;
	constexpr int CARD_SPACING = 10;
	constexpr int GRID_HORIZONTAL_SPACING = 24;
	constexpr int GRID_VERTICAL_SPACING = 10;
	constexpr QLatin1StringView STEP_DONE_RESOURCE{":/illustrations/step_done.svg"};
	constexpr QLatin1StringView STEP_TODO_RESOURCE{":/illustrations/step_todo.svg"};
	constexpr QSize STEP_MARKER_SIZE{24, 24};
	constexpr int STEP_HORIZONTAL_SPACING = 12;
	constexpr int STEP_VERTICAL_SPACING = 6;
	constexpr int STEP_GAP = 14;
	constexpr int STEP_BUTTON_SPACING = 8;
	constexpr int RESTRICTION_NOTICE_SPACING = 4;
	constexpr QStringView REASON_BULLET{u"• "};
	constexpr int INTRO_ROW = 0;
	constexpr int DATA_STEP_ROW = 1;
	constexpr int STEP_GAP_ROW = 4;
	constexpr int SIGN_IN_STEP_ROW = 5;

	QLabel* CreateLabel(QWidget* parent, QLatin1StringView styleName = {}) {
		auto* label = new QLabel(parent);
		if (!styleName.isEmpty()) {
			label->setObjectName(styleName);
		}
		return label;
	}

	void SetStyleName(QLabel& label, QLatin1StringView styleName) {
		if (label.objectName() != styleName) {
			label.setObjectName(styleName);
			label.style()->unpolish(&label);
			label.style()->polish(&label);
		}
	}

	void SetCapabilityState(QLabel& label, bool isAvailable, const QString& text) {
		SetStyleName(label, isAvailable ? CAPABILITY_AVAILABLE_STYLE : CAPABILITY_MISSING_STYLE);
		label.setText(text);
	}
} // namespace

AccountDetailsView::AccountDetailsView(AccountProfiles& profiles, QWidget* parent) :
	QWidget(parent),
	accountProfiles(profiles) {
	nameLabel = CreateLabel(this, PAGE_TITLE_STYLE);
	// The Steam name is chosen by the account owner and is never interpreted as rich text.
	nameLabel->setTextFormat(Qt::PlainText);
	nameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	detailsLabel = CreateLabel(this, SECONDARY_TEXT_STYLE);
	detailsLabel->setTextFormat(Qt::PlainText);
	detailsLabel->setWordWrap(true);
	detailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	restrictionNotice = new QWidget(this);
	restrictionLabel = CreateLabel(restrictionNotice, NOTICE_STYLE);
	// Steam supplies the reasons, so they are never interpreted as rich text.
	restrictionLabel->setTextFormat(Qt::PlainText);
	restrictionLabel->setWordWrap(true);
	restrictionHelpButton = new QPushButton(restrictionNotice);
	restrictionHelpButton->setObjectName(LINK_BUTTON_STYLE);
	restrictionHelpButton->setCursor(Qt::PointingHandCursor);
	auto* restrictionLayout = new QVBoxLayout(restrictionNotice);
	restrictionLayout->setContentsMargins(0, 0, 0, 0);
	restrictionLayout->setSpacing(RESTRICTION_NOTICE_SPACING);
	restrictionLayout->addWidget(restrictionLabel);
	restrictionLayout->addWidget(restrictionHelpButton, 0, Qt::AlignLeft);
	restrictionNotice->hide();

	signInCard = new QFrame(this);
	signInCard->setObjectName(CARD_STYLE);
	signInSectionLabel = CreateLabel(signInCard);
	MakeSectionLabel(*signInSectionLabel);
	signInStatusLabel = CreateLabel(signInCard);
	signInStatusLabel->setWordWrap(true);
	signInButton = new QPushButton(signInCard);
	SetButtonRole(*signInButton, ButtonRole::PRIMARY);
	approveSignInButton = new QPushButton(signInCard);
	SetButtonRole(*approveSignInButton, ButtonRole::PRIMARY);
	signOutButton = new QPushButton(signInCard);
	auto* signInButtonsLayout = new QHBoxLayout();
	signInButtonsLayout->addWidget(signInButton);
	signInButtonsLayout->addWidget(approveSignInButton);
	signInButtonsLayout->addWidget(signOutButton);
	signInButtonsLayout->addStretch();
	auto* signInCardLayout = new QVBoxLayout(signInCard);
	signInCardLayout->setContentsMargins(CARD_MARGINS);
	signInCardLayout->setSpacing(CARD_SPACING);
	signInCardLayout->addWidget(signInSectionLabel);
	signInCardLayout->addWidget(signInStatusLabel);
	signInCardLayout->addLayout(signInButtonsLayout);

	googleCard = new QFrame(this);
	googleCard->setObjectName(CARD_STYLE);
	googleSectionLabel = CreateLabel(googleCard);
	MakeSectionLabel(*googleSectionLabel);
	googleStatusLabel = CreateLabel(googleCard);
	googleStatusLabel->setWordWrap(true);
	googleActionButton = new QPushButton(googleCard);
	auto* googleButtonsLayout = new QHBoxLayout();
	googleButtonsLayout->addWidget(googleActionButton);
	googleButtonsLayout->addStretch();
	auto* googleCardLayout = new QVBoxLayout(googleCard);
	googleCardLayout->setContentsMargins(CARD_MARGINS);
	googleCardLayout->setSpacing(CARD_SPACING);
	googleCardLayout->addWidget(googleSectionLabel);
	googleCardLayout->addWidget(googleStatusLabel);
	googleCardLayout->addLayout(googleButtonsLayout);
	googleCard->hide();
	connect(googleActionButton, &QPushButton::clicked, this, [this]() {
		if (cloudInfo.hasConflict) {
			EmitForShownRecord(&AccountDetailsView::CloudConflictRequested);
		} else if (cloudInfo.isInCloud) {
			EmitForShownRecord(&AccountDetailsView::RemoveFromGoogleRequested);
		} else {
			EmitForShownRecord(&AccountDetailsView::AddToGoogleRequested);
		}
	});

	confirmationsCard = new QFrame(this);
	confirmationsCard->setObjectName(CARD_STYLE);
	confirmationsSectionLabel = CreateLabel(confirmationsCard);
	MakeSectionLabel(*confirmationsSectionLabel);
	confirmationsSummaryLabel = CreateLabel(confirmationsCard);
	confirmationsSummaryLabel->setWordWrap(true);
	openConfirmationsButton = new QPushButton(confirmationsCard);
	SetButtonRole(*openConfirmationsButton, ButtonRole::PRIMARY);
	confirmationsSignInButton = new QPushButton(confirmationsCard);
	SetButtonRole(*confirmationsSignInButton, ButtonRole::PRIMARY);
	setupSteps = new QWidget(confirmationsCard);
	setupIntroLabel = CreateLabel(setupSteps);
	setupIntroLabel->setWordWrap(true);
	dataStepMarker = new IllustrationView(STEP_TODO_RESOURCE, STEP_MARKER_SIZE, setupSteps);
	dataStepTitleLabel = CreateLabel(setupSteps, ROW_NAME_STYLE);
	dataStepTextLabel = CreateLabel(setupSteps, SECONDARY_TEXT_STYLE);
	dataStepTextLabel->setWordWrap(true);
	importFileButton = new QPushButton(setupSteps);
	SetButtonRole(*importFileButton, ButtonRole::PRIMARY);
	enterDataButton = new QPushButton(setupSteps);
	signInStepMarker = new IllustrationView(STEP_TODO_RESOURCE, STEP_MARKER_SIZE, setupSteps);
	signInStepTitleLabel = CreateLabel(setupSteps, ROW_NAME_STYLE);
	signInStepTextLabel = CreateLabel(setupSteps, SECONDARY_TEXT_STYLE);
	signInStepTextLabel->setWordWrap(true);
	stepSignInButton = new QPushButton(setupSteps);
	SetButtonRole(*stepSignInButton, ButtonRole::PRIMARY);
	auto* dataStepButtonsLayout = new QHBoxLayout();
	dataStepButtonsLayout->setSpacing(STEP_BUTTON_SPACING);
	dataStepButtonsLayout->addWidget(importFileButton);
	dataStepButtonsLayout->addWidget(enterDataButton);
	dataStepButtonsLayout->addStretch();
	auto* signInStepButtonsLayout = new QHBoxLayout();
	signInStepButtonsLayout->addWidget(stepSignInButton);
	signInStepButtonsLayout->addStretch();
	auto* setupStepsLayout = new QGridLayout(setupSteps);
	setupStepsLayout->setContentsMargins(0, 0, 0, 0);
	setupStepsLayout->setHorizontalSpacing(STEP_HORIZONTAL_SPACING);
	setupStepsLayout->setVerticalSpacing(STEP_VERTICAL_SPACING);
	setupStepsLayout->addWidget(setupIntroLabel, INTRO_ROW, 0, 1, 2);
	setupStepsLayout->addWidget(dataStepMarker, DATA_STEP_ROW, 0, Qt::AlignTop);
	setupStepsLayout->addWidget(dataStepTitleLabel, DATA_STEP_ROW, 1);
	setupStepsLayout->addWidget(dataStepTextLabel, DATA_STEP_ROW + 1, 1);
	setupStepsLayout->addLayout(dataStepButtonsLayout, DATA_STEP_ROW + 2, 1);
	setupStepsLayout->setRowMinimumHeight(STEP_GAP_ROW, STEP_GAP);
	setupStepsLayout->addWidget(signInStepMarker, SIGN_IN_STEP_ROW, 0, Qt::AlignTop);
	setupStepsLayout->addWidget(signInStepTitleLabel, SIGN_IN_STEP_ROW, 1);
	setupStepsLayout->addWidget(signInStepTextLabel, SIGN_IN_STEP_ROW + 1, 1);
	setupStepsLayout->addLayout(signInStepButtonsLayout, SIGN_IN_STEP_ROW + 2, 1);
	setupStepsLayout->setColumnStretch(1, 1);
	auto* confirmationsButtonsLayout = new QHBoxLayout();
	confirmationsButtonsLayout->addWidget(openConfirmationsButton);
	confirmationsButtonsLayout->addWidget(confirmationsSignInButton);
	confirmationsButtonsLayout->addStretch();
	auto* confirmationsCardLayout = new QVBoxLayout(confirmationsCard);
	confirmationsCardLayout->setContentsMargins(CARD_MARGINS);
	confirmationsCardLayout->setSpacing(CARD_SPACING);
	confirmationsCardLayout->addWidget(confirmationsSectionLabel);
	confirmationsCardLayout->addWidget(confirmationsSummaryLabel);
	confirmationsCardLayout->addWidget(setupSteps);
	confirmationsCardLayout->addLayout(confirmationsButtonsLayout);

	guardCard = new QFrame(this);
	guardCard->setObjectName(CARD_STYLE);
	guardSectionLabel = CreateLabel(guardCard);
	MakeSectionLabel(*guardSectionLabel);
	guardStatusLabel = CreateLabel(guardCard);
	guardStatusLabel->setWordWrap(true);
	setUpGuardButton = new QPushButton(guardCard);
	SetButtonRole(*setUpGuardButton, ButtonRole::PRIMARY);
	moveGuardButton = new QPushButton(guardCard);
	auto* guardButtonsLayout = new QHBoxLayout();
	guardButtonsLayout->addWidget(setUpGuardButton);
	guardButtonsLayout->addWidget(moveGuardButton);
	guardButtonsLayout->addStretch();
	auto* guardCardLayout = new QVBoxLayout(guardCard);
	guardCardLayout->setContentsMargins(CARD_MARGINS);
	guardCardLayout->setSpacing(CARD_SPACING);
	guardCardLayout->addWidget(guardSectionLabel);
	guardCardLayout->addWidget(guardStatusLabel);
	guardCardLayout->addLayout(guardButtonsLayout);

	auto* dataCard = new QFrame(this);
	dataCard->setObjectName(CARD_STYLE);
	capabilitiesSectionLabel = CreateLabel(dataCard);
	MakeSectionLabel(*capabilitiesSectionLabel);
	codesTitleLabel = CreateLabel(dataCard);
	codesStatusLabel = CreateLabel(dataCard);
	codesStatusLabel->setWordWrap(true);
	confirmationsTitleLabel = CreateLabel(dataCard);
	confirmationsStatusLabel = CreateLabel(dataCard);
	confirmationsStatusLabel->setWordWrap(true);
	revocationTitleLabel = CreateLabel(dataCard);
	revocationStatusLabel = CreateLabel(dataCard);
	revocationStatusLabel->setWordWrap(true);
	revocationStatusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	revealRevocationButton = new QPushButton(dataCard);
	MakeCompact(*revealRevocationButton);
	qrCodeButton = new QPushButton(dataCard);
	MakeCompact(*qrCodeButton);

	auto* dataGridLayout = new QGridLayout();
	dataGridLayout->setHorizontalSpacing(GRID_HORIZONTAL_SPACING);
	dataGridLayout->setVerticalSpacing(GRID_VERTICAL_SPACING);
	dataGridLayout->addWidget(codesTitleLabel, 0, 0, Qt::AlignTop);
	dataGridLayout->addWidget(codesStatusLabel, 0, 1, Qt::AlignTop);
	dataGridLayout->addWidget(qrCodeButton, 0, 2, Qt::AlignTop);
	dataGridLayout->addWidget(confirmationsTitleLabel, 1, 0, Qt::AlignTop);
	dataGridLayout->addWidget(confirmationsStatusLabel, 1, 1, Qt::AlignTop);
	dataGridLayout->addWidget(revocationTitleLabel, 2, 0, Qt::AlignTop);
	dataGridLayout->addWidget(revocationStatusLabel, 2, 1, Qt::AlignTop);
	dataGridLayout->addWidget(revealRevocationButton, 2, 2, Qt::AlignTop);
	dataGridLayout->setColumnStretch(1, 1);
	auto* dataCardLayout = new QVBoxLayout(dataCard);
	dataCardLayout->setContentsMargins(CARD_MARGINS);
	dataCardLayout->setSpacing(CARD_SPACING);
	dataCardLayout->addWidget(capabilitiesSectionLabel);
	dataCardLayout->addLayout(dataGridLayout);

	// Like the Manage button of a game in the Steam library: rare actions wait in a menu.
	manageButton = new QPushButton(this);
	manageButton->setObjectName(ICON_BUTTON_STYLE);
	manageButton->setIcon(QIcon(MANAGE_ICON_RESOURCE));
	manageButton->setIconSize(MANAGE_ICON_SIZE);
	manageMenu = new QMenu(manageButton);
	addDataAction = manageMenu->addAction(QString());
	exportAction = manageMenu->addAction(QString());
	qrCodeAction = manageMenu->addAction(QString());
	removeAction = manageMenu->addAction(QString());
	avatarPicture = new PictureView(HEADER_AVATAR_EDGE, this);
	auto* headerLayout = new QGridLayout();
	headerLayout->setHorizontalSpacing(HEADER_HORIZONTAL_SPACING);
	headerLayout->setVerticalSpacing(HEADER_VERTICAL_SPACING);
	headerLayout->addWidget(avatarPicture, 0, 0, 2, 1, Qt::AlignTop);
	headerLayout->addWidget(nameLabel, 0, 1, Qt::AlignBottom);
	headerLayout->addWidget(detailsLabel, 1, 1, Qt::AlignTop);
	headerLayout->addWidget(manageButton, 0, 2, 2, 1, Qt::AlignTop);
	headerLayout->setColumnStretch(1, 1);

	// The page scrolls instead of squeezing the cards, so wrapped texts keep their full height in a low window.
	auto* page = new QWidget(this);
	page->setObjectName(DETAILS_PAGE_STYLE);
	auto* pageLayout = new QVBoxLayout(page);
	pageLayout->setContentsMargins(PAGE_MARGINS);
	pageLayout->setSpacing(PAGE_SPACING);
	pageLayout->addLayout(headerLayout);
	pageLayout->addWidget(restrictionNotice);
	pageLayout->addWidget(signInCard);
	pageLayout->addWidget(confirmationsCard);
	pageLayout->addWidget(googleCard);
	pageLayout->addWidget(guardCard);
	pageLayout->addWidget(dataCard);
	pageLayout->addStretch();
	auto* scrollArea = new QScrollArea(this);
	scrollArea->setObjectName(DETAILS_SCROLL_STYLE);
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setWidgetResizable(true);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollArea->setWidget(page);
	auto* viewLayout = new QVBoxLayout(this);
	viewLayout->setContentsMargins(0, 0, 0, 0);
	viewLayout->addWidget(scrollArea);

	connect(revealRevocationButton, &QPushButton::clicked, this, &AccountDetailsView::ToggleRevocationCode);
	connect(qrCodeButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::QrCodeRequested);
	});
	connect(qrCodeAction, &QAction::triggered, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::QrCodeRequested);
	});
	connect(&accountProfiles, &AccountProfiles::ProfilesChanged, this, &AccountDetailsView::RetranslateUi);
	connect(restrictionHelpButton, &QPushButton::clicked, this, [this]() {
		if (restrictionHelpUrl.isValid()) {
			QDesktopServices::openUrl(restrictionHelpUrl);
		}
	});
	connect(signInButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::SignInRequested);
	});
	connect(approveSignInButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::ApproveSignInRequested);
	});
	connect(openConfirmationsButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::ConfirmationsRequested);
	});
	connect(confirmationsSignInButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::SignInRequested);
	});
	connect(importFileButton, &QPushButton::clicked, this, &AccountDetailsView::ImportRequested);
	connect(enterDataButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::AddDataRequested);
	});
	connect(stepSignInButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::SignInRequested);
	});
	connect(signOutButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::SignOutRequested);
	});
	connect(setUpGuardButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::SetUpGuardRequested);
	});
	connect(moveGuardButton, &QPushButton::clicked, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::MoveGuardRequested);
	});
	connect(manageButton, &QPushButton::clicked, this, [this]() {
		manageMenu->exec(manageButton->mapToGlobal(QPoint(0, manageButton->height())));
	});
	connect(addDataAction, &QAction::triggered, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::AddDataRequested);
	});
	connect(exportAction, &QAction::triggered, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::ExportRequested);
	});
	connect(removeAction, &QAction::triggered, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::RemoveRequested);
	});

	RetranslateUi();
}

void AccountDetailsView::EmitForShownRecord(RecordSignal recordSignal) {
	if (shownRecord.has_value()) {
		Q_EMIT(this->*recordSignal)(shownRecord->id);
	}
}

void AccountDetailsView::ShowRecord(const AuthenticatorRecord& record, const std::optional<AccountSession>& session, const AccountConfirmations* confirmations) {
	if (!shownRecord.has_value() || shownRecord->id != record.id) {
		isRevocationCodeVisible = false;
	}
	shownRecord = record;
	shownSession = session;
	shownConfirmations = confirmations;
	RetranslateUi();
	UpdateGoogleCard();
}

void AccountDetailsView::SetCloudInfo(const RecordCloudInfo& info) {
	cloudInfo = info;
	UpdateGoogleCard();
}

void AccountDetailsView::UpdateGoogleCard() {
	// Without Google, or for an account without codes, there is nothing to sync.
	const bool isShown = cloudInfo.isGoogleConnected && shownRecord.has_value() && DetermineCapabilities(*shownRecord).canGenerateCodes;
	googleCard->setVisible(isShown);
	if (!isShown) {
		return;
	}
	googleSectionLabel->setText(tr("Google"));
	googleActionButton->setVisible(!cloudInfo.isChanging);
	if (cloudInfo.isChanging) {
		googleStatusLabel->setText(tr("Saving to Google…"));
		return;
	}
	if (cloudInfo.hasConflict) {
		googleStatusLabel->setText(tr("Your Google has a different version of this account. Choose which one to keep. Nothing changes until you choose."));
		googleActionButton->setText(tr("Choose…"));
		SetButtonRole(*googleActionButton, ButtonRole::PRIMARY);
		return;
	}
	if (cloudInfo.isInCloud) {
		const QString syncedText = cloudInfo.lastSyncedAt.isValid() ? tr("Last synced at %1.").arg(QLocale().toString(cloudInfo.lastSyncedAt.time(), QLocale::ShortFormat)) : QString();
		googleStatusLabel->setText(tr("An encrypted copy of this account is kept in your Google. It appears by itself on any computer where you connect the same Google.") + (syncedText.isEmpty() ? QString() : QLatin1Char(' ') + syncedText));
		googleActionButton->setText(tr("Remove from Google…"));
		SetButtonRole(*googleActionButton, ButtonRole::SECONDARY);
		return;
	}
	googleStatusLabel->setText(tr("This account is only on this computer. Add it to Google, and it appears on your other computers too."));
	googleActionButton->setText(tr("Add to Google"));
	SetButtonRole(*googleActionButton, ButtonRole::PRIMARY);
}

std::optional<QUuid> AccountDetailsView::ShownRecordId() const {
	if (!shownRecord.has_value()) {
		return std::nullopt;
	}
	return shownRecord->id;
}

void AccountDetailsView::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
		UpdateGoogleCard();
	}
	QWidget::changeEvent(event);
}

void AccountDetailsView::RetranslateUi() {
	signInSectionLabel->setText(tr("Steam sign-in"));
	signInButton->setText(tr("Sign in to Steam…"));
	approveSignInButton->setText(tr("Approve a Steam sign-in…"));
	signOutButton->setText(tr("Sign out"));
	signOutButton->setToolTip(tr("Removes the saved sign-in from this computer."));
	confirmationsSectionLabel->setText(tr("Confirmations"));
	guardSectionLabel->setText(tr("Steam Guard"));
	moveGuardButton->setText(tr("Move from a phone…"));
	capabilitiesSectionLabel->setText(tr("Stored data"));
	codesTitleLabel->setText(tr("Code key"));
	confirmationsTitleLabel->setText(tr("Confirmation key"));
	revocationTitleLabel->setText(tr("Recovery code"));
	revealRevocationButton->setText(isRevocationCodeVisible ? tr("Hide") : tr("Show"));
	addDataAction->setText(tr("Add data…"));
	exportAction->setText(tr("Export to .maFile…"));
	qrCodeAction->setText(tr("QR code for another app…"));
	qrCodeButton->setText(tr("QR code"));
	removeAction->setText(tr("Remove from SteamForge…"));
	manageButton->setToolTip(tr("Manage"));
	manageButton->setAccessibleName(tr("Manage"));
	if (!shownRecord.has_value()) {
		return;
	}

	const AuthenticatorRecord& record = *shownRecord;
	const RecordCapabilities capabilities = DetermineCapabilities(record);
	addDataAction->setVisible(!record.pendingActivation.has_value());
	exportAction->setVisible(DetermineCapabilities(record).canGenerateCodes);
	qrCodeAction->setVisible(DetermineCapabilities(record).canGenerateCodes);
	qrCodeButton->setVisible(DetermineCapabilities(record).canGenerateCodes);
	UpdateHeader(record);
	UpdateRestrictionNotice();

	const bool hasSession = shownSession.has_value();
	const bool isSessionExpired = hasSession && HasExpired(shownSession->refreshToken, QDateTime::currentDateTimeUtc());
	if (isSessionExpired) {
		signInStatusLabel->setText(tr("The saved sign-in of %1 has expired. Sign in to Steam again.").arg(SessionAccountName()));
	} else if (hasSession) {
		const QString signedInAt = QLocale().toString(shownSession->signedInAt.toLocalTime(), QLocale::ShortFormat);
		signInStatusLabel->setText(tr("Signed in as %1 on %2.").arg(SessionAccountName(), signedInAt));
	} else {
		signInStatusLabel->setText(tr("Not signed in. Signing in with the account name and password is needed for confirmations, trades and approving Steam sign-ins on other devices."));
	}
	signInButton->setVisible(!hasSession || isSessionExpired);
	approveSignInButton->setVisible(hasSession && !isSessionExpired);
	approveSignInButton->setEnabled(capabilities.canGenerateCodes);
	signOutButton->setVisible(hasSession);
	// The setup steps of the confirmations card offer the sign-in themselves.
	const bool canUseSession = hasSession && !isSessionExpired;
	const bool isSetupNeeded = capabilities.canGenerateCodes && (!capabilities.canSignConfirmations || !canUseSession);
	signInCard->setVisible(canUseSession || !isSetupNeeded);
	UpdateConfirmationsCard(canUseSession);
	UpdateGuardCard(hasSession && !isSessionExpired);

	const bool isPending = record.pendingActivation.has_value();
	const QString pendingText = tr("Saved, waiting for the setup to finish");
	QString codesText = capabilities.canGenerateCodes ? tr("Saved") : tr("Missing");
	if (isPending) {
		codesText = pendingText;
	}
	SetCapabilityState(*codesStatusLabel, capabilities.canGenerateCodes, codesText);

	QString confirmationsText;
	if (isPending) {
		confirmationsText = pendingText;
	} else if (capabilities.canSignConfirmations) {
		confirmationsText = tr("Saved");
	} else if (!record.secrets.identitySecret.isEmpty()) {
		confirmationsText = tr("Saved, but the SteamID or the device ID is missing");
	} else {
		confirmationsText = tr("Missing");
	}
	SetCapabilityState(*confirmationsStatusLabel, capabilities.canSignConfirmations, confirmationsText);

	QString revocationText = tr("Missing. Keep the recovery code elsewhere: it is needed to remove the authenticator from the account.");
	if (capabilities.hasRevocationCode) {
		revocationText = isRevocationCodeVisible ? record.secrets.revocationCode : tr("Saved");
	}
	SetCapabilityState(*revocationStatusLabel, capabilities.hasRevocationCode, revocationText);
	revealRevocationButton->setVisible(capabilities.hasRevocationCode);
}

void AccountDetailsView::UpdateHeader(const AuthenticatorRecord& record) {
	const AccountProfile profile = accountProfiles.Profile(record.account.steamId);
	shownPersonaName = profile.personaName;
	avatarPicture->SetPicture(profile.avatar);
	// The title is the Steam name or, without one, the account name: the line below never repeats either.
	nameLabel->setText(AccountTitle(record, shownPersonaName));
	detailsLabel->setText(tr("%1 · %2").arg(SteamIdText(record), RecordSourceText(record)));
}

void AccountDetailsView::UpdateGuardCard(bool canUseSession) {
	const AuthenticatorRecord& record = *shownRecord;
	const bool isPending = record.pendingActivation.has_value();
	const bool hasGuardKey = !record.secrets.sharedSecret.isEmpty();
	guardCard->setVisible(isPending || !hasGuardKey);
	if (isPending) {
		if (canUseSession) {
			guardStatusLabel->setText(tr("Setting up Steam Guard is not finished: Steam is waiting for the confirmation code it sent by SMS or email. Until then this account has no codes."));
		} else {
			guardStatusLabel->setText(tr("Setting up Steam Guard is not finished. Sign in to Steam to enter the confirmation code."));
		}
		setUpGuardButton->setText(tr("Finish setup…"));
		setUpGuardButton->setVisible(canUseSession);
		moveGuardButton->setVisible(false);
		return;
	}
	if (canUseSession) {
		guardStatusLabel->setText(tr("SteamForge does not hold Steam Guard of this account. Set it up here if the account has no mobile authenticator, or move it here from the phone that has it: Steam sends an SMS code to the phone of the account."));
	} else {
		guardStatusLabel->setText(tr("SteamForge does not hold Steam Guard of this account. Sign in to Steam to set it up, or move it here from the phone that has it."));
	}
	setUpGuardButton->setText(tr("Set up Steam Guard…"));
	setUpGuardButton->setVisible(canUseSession);
	moveGuardButton->setVisible(true);
}

void AccountDetailsView::UpdateConfirmationsCard(bool canUseSession) {
	const RecordCapabilities capabilities = DetermineCapabilities(*shownRecord);
	const bool isSetupNeeded = capabilities.canGenerateCodes && (!capabilities.canSignConfirmations || !canUseSession);
	confirmationsCard->setVisible(capabilities.canSignConfirmations || isSetupNeeded);
	setupSteps->setVisible(isSetupNeeded);
	confirmationsSummaryLabel->setVisible(!isSetupNeeded);
	if (isSetupNeeded) {
		openConfirmationsButton->setVisible(false);
		confirmationsSignInButton->setVisible(false);
		UpdateSetupSteps(capabilities.canSignConfirmations, canUseSession);
		return;
	}
	if (!capabilities.canSignConfirmations) {
		return;
	}
	const AccountConfirmations* confirmations = shownConfirmations.data();
	const std::optional<ConfirmationsFailure> failure = confirmations != nullptr ? confirmations->LoadFailure() : std::nullopt;
	const bool isSignInRejected = failure.has_value() && failure->kind == ConfirmationsFailureKind::SIGN_IN_REQUIRED;
	openConfirmationsButton->setVisible(canUseSession && confirmations != nullptr && !isSignInRejected);
	openConfirmationsButton->setText(tr("Open confirmations…"));
	confirmationsSignInButton->setVisible(canUseSession && (confirmations == nullptr || isSignInRejected));
	confirmationsSignInButton->setText(tr("Sign in to Steam…"));
	if (!canUseSession) {
		confirmationsSummaryLabel->setText(tr("Sign in to Steam to see and confirm trades and market listings of this account. You'll get a notification about new ones."));
		return;
	}
	if (confirmations == nullptr) {
		confirmationsSummaryLabel->setText(tr("The saved sign-in belongs to another Steam account, so confirmations cannot be checked. Sign in to Steam again."));
		return;
	}
	if (failure.has_value()) {
		if (isSignInRejected) {
			confirmationsSummaryLabel->setText(tr("Steam no longer accepts the saved sign-in, so confirmations cannot be checked. Sign in to Steam again."));
		} else {
			confirmationsSummaryLabel->setText(DescribeWebApiError(tr("The last check of confirmations failed."), failure->error, tr("We'll try again automatically.")));
		}
		return;
	}
	if (!confirmations->HasLoaded()) {
		confirmationsSummaryLabel->setText(tr("Checking confirmations…"));
		return;
	}
	const auto waitingCount = static_cast<int>(confirmations->Confirmations().size());
	if (waitingCount == 0) {
		const QString checkedAt = QLocale().toString(confirmations->LoadedAt().toLocalTime().time(), QLocale::ShortFormat);
		confirmationsSummaryLabel->setText(tr("You have no confirmations right now. Updated at %1.").arg(checkedAt));
		return;
	}
	confirmationsSummaryLabel->setText(tr("%n confirmations are waiting.", nullptr, waitingCount));
	openConfirmationsButton->setText(tr("Open confirmations (%1)…").arg(waitingCount));
}

void AccountDetailsView::UpdateSetupSteps(bool hasConfirmationData, bool canUseSession) {
	setupIntroLabel->setText(tr("To confirm trades and market listings from this computer:"));
	dataStepMarker->SetResource(hasConfirmationData ? STEP_DONE_RESOURCE : STEP_TODO_RESOURCE);
	dataStepTitleLabel->setText(tr("Authenticator data for confirmations"));
	dataStepTextLabel->setText(hasConfirmationData ? tr("Saved.") : tr("Confirmations need the SteamID, the confirmation key and the device ID of this authenticator. They are in its file, a .maFile or twofactor_<SteamID>.json: import the whole file, and only what is missing is added."));
	importFileButton->setText(tr("Import the file…"));
	importFileButton->setVisible(!hasConfirmationData);
	enterDataButton->setText(tr("Enter by hand…"));
	enterDataButton->setVisible(!hasConfirmationData);

	signInStepMarker->SetResource(canUseSession ? STEP_DONE_RESOURCE : STEP_TODO_RESOURCE);
	signInStepTitleLabel->setText(tr("Steam sign-in"));
	if (canUseSession && shownSession.has_value()) {
		signInStepTextLabel->setText(tr("Signed in as %1.").arg(SessionAccountName()));
	} else if (shownSession.has_value()) {
		signInStepTextLabel->setText(tr("The saved sign-in has expired. Sign in again with the account name and password."));
	} else {
		signInStepTextLabel->setText(tr("Sign in once with the account name and password. You won't need to enter a Steam Guard code."));
	}
	stepSignInButton->setText(tr("Sign in to Steam…"));
	stepSignInButton->setVisible(!canUseSession);
}

void AccountDetailsView::UpdateRestrictionNotice() {
	const AccountConfirmations* confirmations = shownConfirmations.data();
	const std::optional<MarketRestriction> restriction = confirmations != nullptr ? confirmations->ActiveMarketRestriction() : std::nullopt;
	const bool hasEnded = restriction.has_value() && restriction->allowedAt.isValid() && restriction->allowedAt <= QDateTime::currentDateTimeUtc();
	restrictionNotice->setVisible(restriction.has_value() && !hasEnded);
	if (!restriction.has_value() || hasEnded) {
		return;
	}
	QStringList lines;
	if (restriction->allowedAt.isValid()) {
		lines.append(tr("The Community Market is unavailable for this account until %1.").arg(QLocale().toString(restriction->allowedAt.toLocalTime(), QLocale::ShortFormat)));
	} else {
		lines.append(tr("The Community Market is unavailable for this account."));
	}
	QStringList explanations;
	restrictionHelpUrl = QUrl();
	// Reasons in another language wait for the new ones instead of mixing two languages.
	if (restriction->reasonsLanguage == InterfaceSteamLanguage()) {
		for (const MarketRestrictionReason& reason : restriction->reasons) {
			lines.append(REASON_BULLET.toString() + reason.text);
			if (!reason.explanation.isEmpty()) {
				explanations.append(reason.explanation);
			}
			if (!restrictionHelpUrl.isValid()) {
				restrictionHelpUrl = reason.helpUrl;
			}
		}
	}
	restrictionLabel->setText(lines.join(QLatin1Char('\n')));
	restrictionLabel->setToolTip(explanations.join(QStringLiteral("\n\n")));
	restrictionHelpButton->setText(tr("Details on the Steam site"));
	restrictionHelpButton->setVisible(restrictionHelpUrl.isValid());
}

void AccountDetailsView::SetAccountNameHidden(bool isHidden) {
	if (isAccountNameHidden == isHidden) {
		return;
	}
	isAccountNameHidden = isHidden;
	RetranslateUi();
}

bool AccountDetailsView::HidesAccountName() const {
	return isAccountNameHidden && shownRecord.has_value() && LeadsWithPersonaName(*shownRecord, shownPersonaName);
}

QString AccountDetailsView::SessionAccountName() const {
	if (!shownSession.has_value()) {
		return {};
	}
	return HidesAccountName() ? HiddenAccountName() : shownSession->accountName;
}

void AccountDetailsView::ToggleRevocationCode() {
	isRevocationCodeVisible = !isRevocationCodeVisible;
	RetranslateUi();
}

} // namespace SteamForge
