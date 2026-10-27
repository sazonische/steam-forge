#include "ui/account_row.h"

#include "app/account_profiles.h"
#include "steam/guard_code.h"
#include "ui/elided_label.h"
#include "ui/record_presentation.h"
#include "ui/steam_picture.h"
#include "ui/theme.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView SHOW_ICON_RESOURCE{":/styles/eye.svg"};
	constexpr QLatin1StringView HIDE_ICON_RESOURCE{":/styles/eye-off.svg"};
	constexpr QLatin1StringView COPY_ICON_RESOURCE{":/styles/copy.svg"};
	constexpr QLatin1StringView COPIED_ICON_RESOURCE{":/styles/copied.svg"};
	constexpr QLatin1StringView CLOUD_SYNCED_ICON_RESOURCE{":/styles/cloud-check.svg"};
	constexpr QLatin1StringView CLOUD_CONFLICT_ICON_RESOURCE{":/styles/cloud-alert.svg"};
	constexpr QLatin1StringView CLOUD_OFF_ICON_RESOURCE{":/styles/cloud-off.svg"};
	constexpr QSize CLOUD_ICON_SIZE{16, 16};
	constexpr int CLOUD_ICON_SPACING = 6;
	constexpr QStringView MASKED_CODE{u"•••••"};
	// Widest possible code, so the row keeps its layout when the code is shown or hidden.
	constexpr QLatin1StringView WIDEST_CODE{"WWWWW"};
	constexpr int CODE_PIXEL_SIZE = 22;
	constexpr QSize ICON_SIZE{18, 18};
	constexpr QMargins ROW_MARGINS{16, 10, 10, 10};
	constexpr int ROW_SPACING = 6;
	constexpr int CODE_SPACING = 14;
	constexpr int AVATAR_EDGE = 36;
	// Added to the row spacing between the avatar and the name.
	constexpr int AVATAR_SPACING = 6;
	constexpr std::chrono::milliseconds COPY_FEEDBACK_DURATION{1500};

	QToolButton* CreateIconButton(QWidget* parent) {
		auto* button = new QToolButton(parent);
		button->setObjectName(ROW_ICON_BUTTON_STYLE);
		button->setIconSize(ICON_SIZE);
		button->setCursor(Qt::PointingHandCursor);
		return button;
	}
} // namespace

AccountRow::AccountRow(AuthenticatorRecord accountRecord, AccountProfiles& profiles, QWidget* parent) :
	QFrame(parent),
	accountProfiles(profiles),
	record(std::move(accountRecord)) {
	setObjectName(ACCOUNT_ROW_STYLE);
	setAttribute(Qt::WA_Hover);
	setFocusPolicy(Qt::StrongFocus);

	avatarPicture = new PictureView(AVATAR_EDGE, this);
	nameLabel = new ElidedLabel(this);
	nameLabel->setObjectName(ROW_NAME_STYLE);
	// As wide as the name while there is room, so the cloud mark stands right after it.
	nameLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	cloudLabel = new QLabel(this);
	cloudLabel->setFixedSize(CLOUD_ICON_SIZE);
	cloudLabel->hide();
	subtitleLabel = new ElidedLabel(this);
	subtitleLabel->setObjectName(ROW_SUBTITLE_STYLE);
	confirmationBadge = new QLabel(this);
	confirmationBadge->setObjectName(ROW_BADGE_STYLE);
	confirmationBadge->setAlignment(Qt::AlignCenter);
	confirmationBadge->hide();
	codeLabel = new QLabel(this);
	codeLabel->setObjectName(ROW_CODE_STYLE);
	codeLabel->setFont(GuardCodeFont(CODE_PIXEL_SIZE));
	codeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	codeLabel->setMinimumWidth(QFontMetrics(codeLabel->font()).horizontalAdvance(WIDEST_CODE));
	revealButton = CreateIconButton(this);
	copyButton = CreateIconButton(this);
	copyButton->setIcon(QIcon(COPY_ICON_RESOURCE));

	auto* nameLayout = new QHBoxLayout();
	nameLayout->setSpacing(CLOUD_ICON_SPACING);
	nameLayout->addWidget(nameLabel);
	nameLayout->addWidget(cloudLabel);
	nameLayout->addStretch();
	auto* textLayout = new QVBoxLayout();
	textLayout->setSpacing(2);
	textLayout->addLayout(nameLayout);
	textLayout->addWidget(subtitleLabel);
	auto* rowLayout = new QHBoxLayout(this);
	rowLayout->setContentsMargins(ROW_MARGINS);
	rowLayout->setSpacing(ROW_SPACING);
	rowLayout->addWidget(avatarPicture);
	rowLayout->addSpacing(AVATAR_SPACING);
	rowLayout->addLayout(textLayout, 1);
	rowLayout->addWidget(confirmationBadge);
	rowLayout->addWidget(codeLabel);
	rowLayout->addSpacing(CODE_SPACING);
	rowLayout->addWidget(revealButton);
	rowLayout->addWidget(copyButton);

	copyFeedbackTimer.setSingleShot(true);
	copyFeedbackTimer.setInterval(COPY_FEEDBACK_DURATION);
	connect(&copyFeedbackTimer, &QTimer::timeout, this, &AccountRow::RetranslateUi);
	connect(revealButton, &QToolButton::clicked, this, &AccountRow::ToggleReveal);
	connect(copyButton, &QToolButton::clicked, this, &AccountRow::CopyCode);
	connect(&accountProfiles, &AccountProfiles::ProfilesChanged, this, &AccountRow::UpdateProfile);

	const bool hasCodeKey = DetermineCapabilities(record).canGenerateCodes;
	codeLabel->setVisible(hasCodeKey);
	copyButton->setVisible(hasCodeKey);
	UpdateProfile();
	RetranslateUi();
	ShowCode();
}

const QUuid& AccountRow::RecordId() const {
	return record.id;
}

void AccountRow::SetCloudState(RowCloudState state) {
	if (cloudState == state) {
		return;
	}
	cloudState = state;
	RetranslateUi();
}

void AccountRow::SetSelected(bool isSelected) {
	SetStyleFlag(*this, StyleFlag::SELECTED, isSelected);
}

void AccountRow::UpdateCode(std::chrono::sys_seconds steamTime) {
	if (!DetermineCapabilities(record).canGenerateCodes) {
		return;
	}
	const std::int64_t timeStep = steamTime.time_since_epoch() / GUARD_CODE_PERIOD;
	if (currentTimeStep == timeStep) {
		return;
	}
	currentTimeStep = timeStep;
	currentCode = GenerateGuardCode(record.secrets.sharedSecret, steamTime);
	ShowCode();
}

void AccountRow::SetConfirmationCount(int waitingCount) {
	if (confirmationCount == waitingCount) {
		return;
	}
	confirmationCount = waitingCount;
	RetranslateUi();
}

void AccountRow::mousePressEvent(QMouseEvent* event) {
	if (event->button() == Qt::LeftButton) {
		setFocus(Qt::MouseFocusReason);
		Q_EMIT SelectionRequested(record.id);
	}
	QFrame::mousePressEvent(event);
}

void AccountRow::keyPressEvent(QKeyEvent* event) {
	if (event->matches(QKeySequence::Copy)) {
		CopyCode();
		return;
	}
	switch (event->key()) {
		case Qt::Key_Up:
			Q_EMIT NeighbourRequested(-1);
			return;
		case Qt::Key_Down:
			Q_EMIT NeighbourRequested(1);
			return;
		case Qt::Key_Space:
			ToggleReveal();
			return;
		default:
			QFrame::keyPressEvent(event);
	}
}

void AccountRow::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QFrame::changeEvent(event);
}

void AccountRow::RetranslateUi() {
	const bool leadsWithPersonaName = LeadsWithPersonaName(record, personaName);
	const QString title = AccountTitle(record, personaName);
	const QString accountName = isRevealed ? RecordDisplayName(record) : HiddenAccountName();
	const QString identity = leadsWithPersonaName ? accountName : SteamIdText(record);
	nameLabel->SetFullText(title);
	setAccessibleName(leadsWithPersonaName && isRevealed ? QStringList{title, accountName}.join(QStringLiteral(", ")) : title);
	QString subtitle = identity;
	if (record.pendingActivation.has_value()) {
		subtitle = tr("%1 · Steam Guard setup not finished").arg(identity);
	} else if (record.secrets.sharedSecret.isEmpty()) {
		subtitle = tr("%1 · Steam Guard not in SteamForge").arg(identity);
	} else if (!DetermineCapabilities(record).canSignConfirmations) {
		subtitle = tr("%1 · codes only").arg(identity);
	}
	subtitleLabel->SetFullText(subtitle);
	cloudLabel->setVisible(cloudState != RowCloudState::NONE);
	if (cloudState == RowCloudState::LOCAL_ONLY) {
		// The usual state, so its mark stays quiet.
		cloudLabel->setPixmap(QIcon(CLOUD_OFF_ICON_RESOURCE).pixmap(CLOUD_ICON_SIZE, devicePixelRatioF(), QIcon::Disabled));
		cloudLabel->setToolTip(tr("Only on this computer"));
	} else if (cloudState == RowCloudState::IN_CLOUD) {
		cloudLabel->setPixmap(QIcon(CLOUD_SYNCED_ICON_RESOURCE).pixmap(CLOUD_ICON_SIZE, devicePixelRatioF()));
		cloudLabel->setToolTip(tr("Saved in Google"));
	} else if (cloudState == RowCloudState::CONFLICT) {
		cloudLabel->setPixmap(QIcon(CLOUD_CONFLICT_ICON_RESOURCE).pixmap(CLOUD_ICON_SIZE, devicePixelRatioF()));
		cloudLabel->setToolTip(tr("Google has a different version: choose which one to keep"));
	}
	cloudLabel->setAccessibleName(cloudLabel->toolTip());

	confirmationBadge->setVisible(confirmationCount > 0);
	confirmationBadge->setText(QString::number(confirmationCount));
	confirmationBadge->setToolTip(tr("%n confirmations waiting", nullptr, confirmationCount));
	confirmationBadge->setAccessibleName(confirmationBadge->toolTip());

	const bool hasCodeKey = DetermineCapabilities(record).canGenerateCodes;
	revealButton->setVisible(hasCodeKey || leadsWithPersonaName);
	QString revealText;
	if (hasCodeKey && leadsWithPersonaName) {
		revealText = isRevealed ? tr("Hide code and account name") : tr("Show code and account name");
	} else if (hasCodeKey) {
		revealText = isRevealed ? tr("Hide code") : tr("Show code");
	} else {
		revealText = isRevealed ? tr("Hide account name") : tr("Show account name");
	}
	revealButton->setIcon(QIcon(isRevealed ? HIDE_ICON_RESOURCE : SHOW_ICON_RESOURCE));
	revealButton->setToolTip(revealText);
	revealButton->setAccessibleName(revealText);

	const bool isCopiedFeedbackShown = copyFeedbackTimer.isActive();
	const QString copyText = isCopiedFeedbackShown ? tr("Copied") : tr("Copy code");
	copyButton->setIcon(QIcon(isCopiedFeedbackShown ? COPIED_ICON_RESOURCE : COPY_ICON_RESOURCE));
	copyButton->setToolTip(copyText);
	copyButton->setAccessibleName(copyText);
}

void AccountRow::ShowCode() {
	if (isRevealed && !currentCode.isEmpty()) {
		codeLabel->setText(currentCode);
	} else {
		codeLabel->setText(MASKED_CODE.toString());
	}
}

void AccountRow::SetRevealed(bool isShown) {
	isRevealed = isShown;
	ShowCode();
	RetranslateUi();
}

void AccountRow::ToggleReveal() {
	SetRevealed(!isRevealed);
	Q_EMIT RevealToggled(record.id, isRevealed);
}

void AccountRow::CopyCode() {
	if (currentCode.isEmpty()) {
		return;
	}
	QGuiApplication::clipboard()->setText(currentCode);
	copyFeedbackTimer.start();
	RetranslateUi();
}

void AccountRow::UpdateProfile() {
	const AccountProfile profile = accountProfiles.Profile(record.account.steamId);
	avatarPicture->SetPicture(profile.avatar);
	if (profile.personaName != personaName) {
		personaName = profile.personaName;
		RetranslateUi();
	}
}

} // namespace SteamForge
