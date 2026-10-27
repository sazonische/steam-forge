#include "ui/cloud_conflict_dialog.h"

#include "steam/guard_code.h"
#include "steam/steam_clock.h"
#include "ui/theme.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int MINIMUM_DIALOG_WIDTH = 580;
	constexpr int DIALOG_SPACING = 12;
	constexpr int CARDS_SPACING = 12;
	constexpr int CARD_SPACING = 6;
	constexpr int CODE_PIXEL_SIZE = 26;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins CARD_MARGINS{16, 12, 16, 14};
	// A code lives thirty seconds; checking every second shows the new one at once.
	constexpr std::chrono::milliseconds CODE_REFRESH_INTERVAL{std::chrono::seconds(1)};
} // namespace

CloudConflictDialog::CloudConflictDialog(const CloudConflict& conflict, const AuthenticatorRecord& localRecord, const QString& accountTitle, const SteamClock& clock, QWidget* parent) :
	QDialog(parent),
	steamClock(clock),
	cloudRecordId(conflict.cloudRecordId),
	localSharedSecret(localRecord.secrets.sharedSecret),
	cloudSharedSecret(conflict.cloudRecord.secrets.sharedSecret) {
	setWindowTitle(tr("Choose a version"));
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);
	const bool isOtherKey = conflict.kind == CloudConflictKind::OTHER_KEY_OF_ACCOUNT;

	auto* headlineLabel = new QLabel(isOtherKey ? tr("%1 is here and in Google").arg(accountTitle) : tr("%1 changed here and on another computer").arg(accountTitle), this);
	headlineLabel->setObjectName(PAGE_TITLE_STYLE);
	headlineLabel->setWordWrap(true);
	auto* explanationLabel = new QLabel(isOtherKey ? tr("These are two different Steam Guards, and Steam accepts only one of their codes. Sign in to Steam with each code to see which one works.") : tr("Both versions have the same Steam Guard. Choose the version every computer keeps."), this);
	explanationLabel->setWordWrap(true);

	auto* cardsLayout = new QHBoxLayout();
	cardsLayout->setSpacing(CARDS_SPACING);
	cardsLayout->addWidget(CreateVersionCard(tr("On this computer"), localRecord, localCodeLabel), 1);
	cardsLayout->addWidget(CreateVersionCard(tr("In Google"), conflict.cloudRecord, cloudCodeLabel), 1);

	auto* choicesLayout = new QVBoxLayout();
	const auto addChoice = [this, choicesLayout](ConflictChoice choice, const QString& text) {
		auto* choiceButton = new QRadioButton(text, this);
		choiceGroup.addButton(choiceButton, static_cast<int>(choice));
		choicesLayout->addWidget(choiceButton);
		return choiceButton;
	};
	if (isOtherKey) {
		addChoice(ConflictChoice::KEEP_BOTH, tr("Keep both as separate accounts"))->setChecked(true);
	}
	addChoice(ConflictChoice::USE_CLOUD, tr("Use the version from Google here; this one is kept as a backup"));
	addChoice(ConflictChoice::USE_LOCAL, tr("Put this version into Google instead"));

	applyButton = new QPushButton(tr("Apply"), this);
	SetButtonRole(*applyButton, ButtonRole::PRIMARY);
	applyButton->setEnabled(choiceGroup.checkedButton() != nullptr);
	auto* laterButton = new QPushButton(tr("Later"), this);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(applyButton);
	buttonsLayout->addWidget(laterButton);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(headlineLabel);
	dialogLayout->addWidget(explanationLabel);
	dialogLayout->addLayout(cardsLayout);
	dialogLayout->addLayout(choicesLayout);
	dialogLayout->addLayout(buttonsLayout);

	connect(&choiceGroup, &QButtonGroup::idClicked, this, [this]() {
		applyButton->setEnabled(true);
	});
	connect(applyButton, &QPushButton::clicked, this, &QDialog::accept);
	connect(laterButton, &QPushButton::clicked, this, &QDialog::reject);
	codeTimer.setInterval(CODE_REFRESH_INTERVAL);
	connect(&codeTimer, &QTimer::timeout, this, &CloudConflictDialog::UpdateCodes);
	codeTimer.start();
	UpdateCodes();
}

std::optional<ConflictChoice> CloudConflictDialog::Choice() const {
	if (choiceGroup.checkedId() < 0) {
		return std::nullopt;
	}
	return static_cast<ConflictChoice>(choiceGroup.checkedId());
}

const QString& CloudConflictDialog::CloudRecordId() const {
	return cloudRecordId;
}

QFrame* CloudConflictDialog::CreateVersionCard(const QString& title, const AuthenticatorRecord& record, QLabel*& codeLabel) {
	auto* card = new QFrame(this);
	card->setObjectName(CARD_STYLE);
	auto* titleLabel = new QLabel(title, card);
	MakeSectionLabel(*titleLabel);
	codeLabel = new QLabel(card);
	codeLabel->setFont(GuardCodeFont(CODE_PIXEL_SIZE));
	codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	const QDateTime addedAt = record.source.importedAt.isValid() ? record.source.importedAt : record.createdAt;
	QStringList facts;
	if (addedAt.isValid()) {
		facts.append(tr("Added %1").arg(QLocale().toString(addedAt.toLocalTime(), QLocale::ShortFormat)));
	}
	facts.append(DetermineCapabilities(record).canSignConfirmations ? tr("Codes and confirmations") : tr("Codes only"));
	auto* factsLabel = new QLabel(facts.join(QLatin1Char('\n')), card);
	factsLabel->setObjectName(SECONDARY_TEXT_STYLE);
	auto* cardLayout = new QVBoxLayout(card);
	cardLayout->setContentsMargins(CARD_MARGINS);
	cardLayout->setSpacing(CARD_SPACING);
	cardLayout->addWidget(titleLabel);
	cardLayout->addWidget(codeLabel);
	cardLayout->addWidget(factsLabel);
	return card;
}

void CloudConflictDialog::UpdateCodes() {
	const auto steamTime = std::chrono::floor<std::chrono::seconds>(steamClock.Now());
	localCodeLabel->setText(GenerateGuardCode(localSharedSecret, steamTime));
	cloudCodeLabel->setText(GenerateGuardCode(cloudSharedSecret, steamTime));
}

} // namespace SteamForge
