#include "ui/account_details_view.h"

#include "app/account_profiles.h"
#include "steam/jwt_claims.h"
#include "ui/illustration_view.h"
#include "ui/image_resources.h"
#include "ui/record_presentation.h"
#include "ui/steam_picture.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int HEADER_AVATAR_EDGE = 64;
	constexpr int HEADER_HORIZONTAL_SPACING = 16;
	constexpr int HEADER_VERTICAL_SPACING = 4;
	constexpr QSize MANAGE_ICON_SIZE{18, 18};
	constexpr QMargins PAGE_MARGINS{32, 24, 32, 20};
	constexpr QMargins CARD_MARGINS{24, 16, 24, 16};
	// A card of one line, such as the copy in Google.
	constexpr QMargins ROW_CARD_MARGINS{24, 10, 24, 10};
	constexpr int ROW_CARD_SPACING = 14;
	constexpr int STATE_HORIZONTAL_SPACING = 14;
	constexpr int STATE_VERTICAL_SPACING = 2;
	constexpr QSize STATE_ICON_SIZE{24, 24};
	constexpr QSize GOOGLE_ICON_SIZE{28, 28};
	// A saved secret, shown only as being there.
	constexpr QStringView MASKED_SECRET{u"\u2022\u2022\u2022\u2022\u2022"};
	constexpr int CODES_ROW = 0;
	constexpr int CONFIRMATION_KEY_ROW = 2;
	constexpr int RECOVERY_CODE_ROW = 4;
	constexpr int DATA_COLUMN_COUNT = 3;
	// The room above and below a divider of the secret data.
	constexpr int DATA_ROW_SPACING = 10;
	constexpr int PAGE_SPACING = 12;
	constexpr int CARD_SPACING = 10;
	constexpr int GRID_HORIZONTAL_SPACING = 24;
	constexpr QSize STEP_MARKER_SIZE{24, 24};
	constexpr int STEP_HORIZONTAL_SPACING = 12;
	constexpr int STEP_VERTICAL_SPACING = 6;
	constexpr int STEP_GAP = 14;
	constexpr int STEP_BUTTON_SPACING = 8;
	constexpr int RESTRICTION_NOTICE_SPACING = 4;
	constexpr int INTRO_ROW = 0;
	constexpr int DATA_STEP_ROW = 1;
	constexpr int STEP_GAP_ROW = 4;
	constexpr int SIGN_IN_STEP_ROW = 5;

	void SetCapabilityState(QLabel& label, bool isAvailable, const QString& text) {
		QLatin1StringView styleName = isAvailable ? CAPABILITY_AVAILABLE_STYLE : CAPABILITY_MISSING_STYLE;
		if (text == MASKED_SECRET) {
			styleName = MASKED_SECRET_STYLE;
		}
		SetStyleName(label, styleName);
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
	steamIdLabel = CreateLabel(this, DETAIL_TEXT_STYLE);
	steamIdLabel->setTextFormat(Qt::PlainText);
	steamIdLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	originLabel = CreateLabel(this, DETAIL_TEXT_STYLE);
	originLabel->setTextFormat(Qt::PlainText);
	originLabel->setWordWrap(true);
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
	signInDot = CreateLabel(signInCard, STATE_DOT_IDLE_STYLE);
	signInHeadlineLabel = CreateLabel(signInCard, STATE_HEADLINE_STYLE);
	signInStatusLabel = CreateLabel(signInCard, DETAIL_TEXT_STYLE);
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
	auto* signInStateLayout = new QGridLayout();
	signInStateLayout->setHorizontalSpacing(STATE_HORIZONTAL_SPACING);
	signInStateLayout->setVerticalSpacing(STATE_VERTICAL_SPACING);
	signInStateLayout->addWidget(signInDot, 0, 0, Qt::AlignCenter);
	signInStateLayout->addWidget(signInHeadlineLabel, 0, 1);
	signInStateLayout->addWidget(signInStatusLabel, 1, 1);
	signInStateLayout->setColumnStretch(1, 1);
	auto* signInCardLayout = new QVBoxLayout(signInCard);
	signInCardLayout->setContentsMargins(CARD_MARGINS);
	signInCardLayout->setSpacing(CARD_SPACING);
	signInCardLayout->addWidget(signInSectionLabel);
	signInCardLayout->addLayout(signInStateLayout);
	signInCardLayout->addLayout(signInButtonsLayout);

	// One line: the state of the copy, with the explanation in its tooltip and the rare action in a menu.
	googleCard = new QFrame(this);
	googleCard->setObjectName(CARD_STYLE);
	googleIcon = new IllustrationView(CLOUD_CHECK_ICON_RESOURCE, GOOGLE_ICON_SIZE, googleCard);
	googleTitleLabel = CreateLabel(googleCard, STATE_HEADLINE_STYLE);
	googleStatusLabel = CreateLabel(googleCard, DETAIL_TEXT_STYLE);
	// Wraps in a narrow window instead of widening the page past its edge.
	googleStatusLabel->setWordWrap(true);
	googleActionButton = new QPushButton(googleCard);
	googleMenuButton = new QPushButton(googleCard);
	googleMenuButton->setObjectName(ICON_BUTTON_STYLE);
	googleMenuButton->setIcon(QIcon(MORE_ICON_RESOURCE));
	googleMenuButton->setIconSize(MANAGE_ICON_SIZE);
	MakeCompact(*googleMenuButton);
	googleMenu = new QMenu(googleMenuButton);
	removeFromGoogleAction = googleMenu->addAction(QString());
	auto* googleCardLayout = new QHBoxLayout(googleCard);
	googleCardLayout->setContentsMargins(ROW_CARD_MARGINS);
	googleCardLayout->setSpacing(ROW_CARD_SPACING);
	googleCardLayout->addWidget(googleIcon);
	googleCardLayout->addWidget(googleTitleLabel);
	googleCardLayout->addWidget(googleStatusLabel, 1);
	googleCardLayout->addWidget(googleActionButton);
	googleCardLayout->addWidget(googleMenuButton);
	googleCard->hide();
	connect(googleActionButton, &QPushButton::clicked, this, [this]() {
		if (cloudInfo.hasConflict) {
			EmitForShownRecord(&AccountDetailsView::CloudConflictRequested);
		} else {
			EmitForShownRecord(&AccountDetailsView::AddToGoogleRequested);
		}
	});
	connect(googleMenuButton, &QPushButton::clicked, this, [this]() {
		googleMenu->exec(googleMenuButton->mapToGlobal(QPoint(0, googleMenuButton->height())));
	});
	connect(removeFromGoogleAction, &QAction::triggered, this, [this]() {
		EmitForShownRecord(&AccountDetailsView::RemoveFromGoogleRequested);
	});

	confirmationsCard = new QFrame(this);
	confirmationsCard->setObjectName(CARD_STYLE);
	confirmationsSectionLabel = CreateLabel(confirmationsCard);
	MakeSectionLabel(*confirmationsSectionLabel);
	confirmationsState = new QWidget(confirmationsCard);
	confirmationsIcon = new IllustrationView(CHECK_CIRCLE_ICON_RESOURCE, STATE_ICON_SIZE, confirmationsState);
	confirmationsCountBadge = CreateLabel(confirmationsState, ROW_BADGE_STYLE);
	confirmationsCountBadge->setAlignment(Qt::AlignCenter);
	confirmationsSummaryLabel = CreateLabel(confirmationsState);
	confirmationsSummaryLabel->setWordWrap(true);
	confirmationsDetailLabel = CreateLabel(confirmationsState, DETAIL_TEXT_STYLE);
	auto* confirmationsStateLayout = new QGridLayout(confirmationsState);
	confirmationsStateLayout->setContentsMargins(0, 0, 0, 0);
	confirmationsStateLayout->setHorizontalSpacing(STATE_HORIZONTAL_SPACING);
	confirmationsStateLayout->setVerticalSpacing(STATE_VERTICAL_SPACING);
	confirmationsStateLayout->addWidget(confirmationsIcon, 0, 0, 2, 1, Qt::AlignCenter);
	confirmationsStateLayout->addWidget(confirmationsCountBadge, 0, 0, 2, 1, Qt::AlignCenter);
	confirmationsStateLayout->addWidget(confirmationsSummaryLabel, 0, 1);
	confirmationsStateLayout->addWidget(confirmationsDetailLabel, 1, 1);
	confirmationsStateLayout->setColumnStretch(1, 1);
	openConfirmationsButton = new QPushButton(confirmationsCard);
	confirmationsSignInButton = new QPushButton(confirmationsCard);
	SetButtonRole(*confirmationsSignInButton, ButtonRole::PRIMARY);
	setupSteps = new QWidget(confirmationsCard);
	setupIntroLabel = CreateLabel(setupSteps);
	setupIntroLabel->setWordWrap(true);
	dataStepMarker = new IllustrationView(STEP_TODO_ILLUSTRATION_RESOURCE, STEP_MARKER_SIZE, setupSteps);
	dataStepTitleLabel = CreateLabel(setupSteps, ROW_NAME_STYLE);
	dataStepTextLabel = CreateLabel(setupSteps, SECONDARY_TEXT_STYLE);
	dataStepTextLabel->setWordWrap(true);
	importFileButton = new QPushButton(setupSteps);
	SetButtonRole(*importFileButton, ButtonRole::PRIMARY);
	enterDataButton = new QPushButton(setupSteps);
	signInStepMarker = new IllustrationView(STEP_TODO_ILLUSTRATION_RESOURCE, STEP_MARKER_SIZE, setupSteps);
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
	confirmationsCardLayout->addWidget(confirmationsState);
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
	qrCodeButton = new QPushButton(dataCard);

	auto* dataGridLayout = new QGridLayout();
	dataGridLayout->setHorizontalSpacing(GRID_HORIZONTAL_SPACING);
	dataGridLayout->setVerticalSpacing(DATA_ROW_SPACING);
	dataGridLayout->addWidget(codesTitleLabel, CODES_ROW, 0, Qt::AlignVCenter);
	dataGridLayout->addWidget(codesStatusLabel, CODES_ROW, 1, Qt::AlignVCenter);
	dataGridLayout->addWidget(qrCodeButton, CODES_ROW, 2, Qt::AlignVCenter);
	dataGridLayout->addWidget(confirmationsTitleLabel, CONFIRMATION_KEY_ROW, 0, Qt::AlignVCenter);
	dataGridLayout->addWidget(confirmationsStatusLabel, CONFIRMATION_KEY_ROW, 1, Qt::AlignVCenter);
	dataGridLayout->addWidget(revocationTitleLabel, RECOVERY_CODE_ROW, 0, Qt::AlignVCenter);
	dataGridLayout->addWidget(revocationStatusLabel, RECOVERY_CODE_ROW, 1, Qt::AlignVCenter);
	dataGridLayout->addWidget(revealRevocationButton, RECOVERY_CODE_ROW, 2, Qt::AlignVCenter);
	for (const int dividerRow : {CODES_ROW + 1, CONFIRMATION_KEY_ROW + 1}) {
		dataGridLayout->addWidget(CreateRowDivider(dataCard), dividerRow, 0, 1, DATA_COLUMN_COUNT);
	}
	// A row without a button is as tall as one with a button, so the dividers stay evenly apart.
	qrCodeButton->ensurePolished();
	for (const int dataRow : {CODES_ROW, CONFIRMATION_KEY_ROW, RECOVERY_CODE_ROW}) {
		dataGridLayout->setRowMinimumHeight(dataRow, qrCodeButton->sizeHint().height());
	}
	dataGridLayout->setColumnStretch(1, 1);
	auto* dataCardLayout = new QVBoxLayout(dataCard);
	dataCardLayout->setContentsMargins(CARD_MARGINS);
	dataCardLayout->setSpacing(CARD_SPACING);
	dataCardLayout->addWidget(capabilitiesSectionLabel);
	dataCardLayout->addLayout(dataGridLayout);

	// Like the Manage button of a game in the Steam library: rare actions wait in a menu.
	manageButton = new QPushButton(this);
	manageButton->setObjectName(ICON_BUTTON_STYLE);
	manageButton->setIcon(QIcon(GEAR_ICON_RESOURCE));
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
	headerLayout->addWidget(avatarPicture, 0, 0, 3, 1, Qt::AlignTop);
	headerLayout->addWidget(nameLabel, 0, 1, Qt::AlignBottom);
	headerLayout->addWidget(steamIdLabel, 1, 1);
	headerLayout->addWidget(originLabel, 2, 1, Qt::AlignTop);
	headerLayout->addWidget(manageButton, 0, 2, 3, 1, Qt::AlignTop);
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
	googleTitleLabel->setText(tr("Copy in Google"));
	googleMenuButton->setToolTip(tr("More"));
	googleMenuButton->setAccessibleName(tr("More"));
	removeFromGoogleAction->setText(tr("Remove from Google…"));
	googleActionButton->hide();
	googleMenuButton->hide();
	if (cloudInfo.isChanging) {
		googleIcon->SetResource(CLOUD_ICON_RESOURCE);
		googleStatusLabel->setText(tr("Saving to Google…"));
		googleCard->setToolTip(QString());
		return;
	}
	if (cloudInfo.hasConflict) {
		googleIcon->SetResource(CLOUD_ALERT_ICON_RESOURCE);
		googleStatusLabel->setText(tr("Google has a different version."));
		googleCard->setToolTip(tr("Your Google has a different version of this account. Choose which one to keep. Nothing changes until you choose."));
		googleActionButton->setText(tr("Choose…"));
		SetButtonRole(*googleActionButton, ButtonRole::PRIMARY);
		googleActionButton->show();
		return;
	}
	if (cloudInfo.isInCloud) {
		googleIcon->SetResource(CLOUD_CHECK_ICON_RESOURCE);
		googleStatusLabel->setText(cloudInfo.lastSyncedAt.isValid() ? tr("Synced at %1.").arg(QLocale().toString(cloudInfo.lastSyncedAt.time(), QLocale::ShortFormat)) : tr("Synced."));
		googleCard->setToolTip(tr("An encrypted copy of this account is kept in your Google. It appears by itself on any computer where you connect the same Google."));
		googleMenuButton->show();
		return;
	}
	googleIcon->SetResource(CLOUD_OFF_ICON_RESOURCE);
	googleStatusLabel->setText(tr("Only on this computer."));
	googleCard->setToolTip(tr("This account is only on this computer. Add it to Google, and it appears on your other computers too."));
	googleActionButton->setText(tr("Add to Google"));
	SetButtonRole(*googleActionButton, ButtonRole::SECONDARY);
	googleActionButton->show();
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
	capabilitiesSectionLabel->setText(tr("Secret data"));
	codesTitleLabel->setText(tr("Steam Guard key"));
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
		SetStyleName(*signInDot, STATE_DOT_ATTENTION_STYLE);
		signInHeadlineLabel->setText(tr("Sign-in expired"));
		signInStatusLabel->setText(tr("The saved sign-in of %1 has expired. Sign in to Steam again.").arg(SessionAccountName()));
	} else if (hasSession) {
		const QString signedInAt = QLocale().toString(shownSession->signedInAt.toLocalTime(), QLocale::ShortFormat);
		SetStyleName(*signInDot, STATE_DOT_DONE_STYLE);
		signInHeadlineLabel->setText(tr("Signed in"));
		signInStatusLabel->setText(tr("As %1 on this computer since %2.").arg(SessionAccountName(), signedInAt));
	} else {
		SetStyleName(*signInDot, STATE_DOT_IDLE_STYLE);
		signInHeadlineLabel->setText(tr("Not signed in"));
		signInStatusLabel->setText(tr("Signing in with the account name and password is needed for confirmations, trades and approving Steam sign-ins on other devices."));
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
	QString codesText = capabilities.canGenerateCodes ? MASKED_SECRET.toString() : tr("Missing");
	if (isPending) {
		codesText = pendingText;
	}
	SetCapabilityState(*codesStatusLabel, capabilities.canGenerateCodes, codesText);

	QString confirmationsText;
	if (isPending) {
		confirmationsText = pendingText;
	} else if (capabilities.canSignConfirmations) {
		confirmationsText = MASKED_SECRET.toString();
	} else if (!record.secrets.identitySecret.isEmpty()) {
		confirmationsText = tr("Saved, but the SteamID or the device ID is missing");
	} else {
		confirmationsText = tr("Missing");
	}
	SetCapabilityState(*confirmationsStatusLabel, capabilities.canSignConfirmations, confirmationsText);

	QString revocationText = tr("Missing. Keep the recovery code elsewhere: it is needed to remove the authenticator from the account.");
	if (capabilities.hasRevocationCode) {
		revocationText = isRevocationCodeVisible ? record.secrets.revocationCode : MASKED_SECRET.toString();
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
	steamIdLabel->setText(SteamIdText(record));
	originLabel->setText(RecordOriginText(record));
	// The file and the date of the import stay at hand without taking a line.
	QString sourceText = RecordSourceText(record);
	if (!sourceText.isEmpty()) {
		sourceText[0] = sourceText.at(0).toUpper();
	}
	originLabel->setToolTip(sourceText);
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
	confirmationsState->setVisible(!isSetupNeeded);
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
	SetButtonRole(*openConfirmationsButton, ButtonRole::SECONDARY);
	confirmationsSignInButton->setVisible(canUseSession && (confirmations == nullptr || isSignInRejected));
	confirmationsSignInButton->setText(tr("Sign in to Steam…"));
	if (!canUseSession) {
		ShowConfirmationsState({}, tr("Sign in to Steam to see and confirm trades and market listings of this account. You'll get a notification about new ones."));
		return;
	}
	if (confirmations == nullptr) {
		ShowConfirmationsState(ALERT_CIRCLE_ICON_RESOURCE, tr("The saved sign-in belongs to another Steam account, so confirmations cannot be checked. Sign in to Steam again."));
		return;
	}
	if (failure.has_value()) {
		if (isSignInRejected) {
			ShowConfirmationsState(ALERT_CIRCLE_ICON_RESOURCE, tr("Steam no longer accepts the saved sign-in, so confirmations cannot be checked. Sign in to Steam again."));
		} else {
			ShowConfirmationsState(ALERT_CIRCLE_ICON_RESOURCE, DescribeWebApiError(tr("The last check of confirmations failed."), failure->error, tr("We'll try again automatically.")));
		}
		return;
	}
	if (!confirmations->HasLoaded()) {
		ShowConfirmationsState(CLOCK_ICON_RESOURCE, tr("Checking confirmations…"));
		return;
	}
	const QString updatedText = tr("Updated at %1.").arg(QLocale().toString(confirmations->LoadedAt().toLocalTime().time(), QLocale::ShortFormat));
	const auto waitingCount = static_cast<int>(confirmations->Confirmations().size());
	if (waitingCount == 0) {
		ShowConfirmationsState(CHECK_CIRCLE_ICON_RESOURCE, tr("You have no confirmations right now."), updatedText);
		return;
	}
	ShowConfirmationsState({}, tr("%n confirmations are waiting.", nullptr, waitingCount), updatedText);
	confirmationsCountBadge->setText(QString::number(waitingCount));
	confirmationsCountBadge->show();
	// Waiting confirmations are the one thing on the page to act on.
	openConfirmationsButton->setText(tr("Open confirmations (%1)…").arg(waitingCount));
	SetButtonRole(*openConfirmationsButton, ButtonRole::PRIMARY);
}

void AccountDetailsView::ShowConfirmationsState(QLatin1StringView iconResource, const QString& headline, const QString& detail) {
	confirmationsCountBadge->hide();
	confirmationsIcon->setVisible(!iconResource.isEmpty());
	if (!iconResource.isEmpty()) {
		confirmationsIcon->SetResource(iconResource);
	}
	confirmationsSummaryLabel->setText(headline);
	confirmationsDetailLabel->setText(detail);
	confirmationsDetailLabel->setVisible(!detail.isEmpty());
}

void AccountDetailsView::UpdateSetupSteps(bool hasConfirmationData, bool canUseSession) {
	setupIntroLabel->setText(tr("To confirm trades and market listings from this computer:"));
	dataStepMarker->SetResource(hasConfirmationData ? STEP_DONE_ILLUSTRATION_RESOURCE : STEP_TODO_ILLUSTRATION_RESOURCE);
	dataStepTitleLabel->setText(tr("Authenticator data for confirmations"));
	dataStepTextLabel->setText(hasConfirmationData ? tr("Saved.") : tr("Confirmations need the SteamID, the confirmation key and the device ID of this authenticator. They are in its file, a .maFile or twofactor_<SteamID>.json: import the whole file, and only what is missing is added."));
	importFileButton->setText(tr("Import the file…"));
	importFileButton->setVisible(!hasConfirmationData);
	enterDataButton->setText(tr("Enter by hand…"));
	enterDataButton->setVisible(!hasConfirmationData);

	signInStepMarker->SetResource(canUseSession ? STEP_DONE_ILLUSTRATION_RESOURCE : STEP_TODO_ILLUSTRATION_RESOURCE);
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
	QStringList reasonTexts;
	QStringList explanations;
	restrictionHelpUrl = QUrl();
	// Reasons in another language wait for the new ones instead of mixing two languages.
	if (restriction->reasonsLanguage == InterfaceSteamLanguage()) {
		for (const MarketRestrictionReason& reason : restriction->reasons) {
			reasonTexts.append(reason.text);
			if (!reason.explanation.isEmpty()) {
				explanations.append(reason.explanation);
			}
			if (!restrictionHelpUrl.isValid()) {
				restrictionHelpUrl = reason.helpUrl;
			}
		}
	}
	if (!reasonTexts.isEmpty()) {
		lines.append(BulletLines(reasonTexts));
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
