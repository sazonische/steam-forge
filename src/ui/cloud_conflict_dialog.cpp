#include "ui/cloud_conflict_dialog.h"

#include "steam/guard_code.h"
#include "steam/steam_clock.h"
#include "ui/row_list.h"
#include "ui/theme.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int MINIMUM_DIALOG_WIDTH = 640;
	constexpr int DIALOG_SPACING = 12;
	constexpr int CARDS_SPACING = 12;
	constexpr int CARD_SPACING = 6;
	constexpr int CODE_PIXEL_SIZE = 28;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins CARD_MARGINS{20, 14, 20, 16};
	constexpr QMargins CHOICE_MARGINS{16, 10, 16, 10};
	constexpr int CHOICE_HORIZONTAL_SPACING = 14;
	constexpr int CHOICE_VERTICAL_SPACING = 2;
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

	auto* headlineLabel = new QLabel(tr("Which version of %1 to keep?").arg(accountTitle), this);
	headlineLabel->setObjectName(PAGE_TITLE_STYLE);
	headlineLabel->setWordWrap(true);
	auto* explanationLabel = new QLabel(isOtherKey ? tr("This computer and Google keep different Steam Guards. Steam accepts the codes of only one of them.") : tr("The account changed here and on another computer. Both versions have the same Steam Guard."), this);
	explanationLabel->setObjectName(DETAIL_TEXT_STYLE);
	explanationLabel->setWordWrap(true);

	auto* cardsLayout = new QHBoxLayout();
	cardsLayout->setSpacing(CARDS_SPACING);
	cardsLayout->addWidget(CreateVersionCard(tr("On this computer"), localRecord, localCodeLabel), 1);
	cardsLayout->addWidget(CreateVersionCard(tr("In Google"), conflict.cloudRecord, cloudCodeLabel), 1);
	// Only different Steam Guards have different codes to try.
	auto* checkHintLabel = new QLabel(tr("Check the Steam sign-in with each code."), this);
	checkHintLabel->setVisible(isOtherKey);

	QFrame* choicesFrame = CreateListFrame(this);
	auto* choiceList = new RowList(choicesFrame);
	choicesFrame->layout()->addWidget(choiceList);
	const auto addChoice = [this, choiceList](ConflictChoice choice, const QString& title, const QString& description) {
		choiceList->AddRow(CreateChoiceRow(choiceList, choice, title, description));
	};
	if (isOtherKey) {
		addChoice(ConflictChoice::KEEP_BOTH, tr("Keep both versions"), tr("They are kept as separate accounts."));
		choiceGroup.button(static_cast<int>(ConflictChoice::KEEP_BOTH))->setChecked(true);
	}
	// A key is backed up only when it is replaced; the same Steam Guard keeps its key.
	addChoice(ConflictChoice::USE_CLOUD, tr("Use the version from Google"), isOtherKey ? tr("The Steam Guard here is kept in a backup.") : tr("The changes made here are replaced."));
	addChoice(ConflictChoice::USE_LOCAL, tr("Use the version from this computer"), tr("It replaces the version in Google, also on your other computers."));

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
	dialogLayout->addWidget(checkHintLabel);
	dialogLayout->addWidget(choicesFrame);
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

bool CloudConflictDialog::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::MouseButtonPress) {
		if (QRadioButton* choiceButton = choiceButtonsByRow.value(watched); choiceButton != nullptr) {
			choiceButton->click();
		}
	}
	return QDialog::eventFilter(watched, event);
}

QFrame* CloudConflictDialog::CreateVersionCard(const QString& title, const AuthenticatorRecord& record, QLabel*& codeLabel) {
	auto* card = new QFrame(this);
	card->setObjectName(CARD_STYLE);
	auto* titleLabel = new QLabel(title, card);
	codeLabel = new QLabel(card);
	codeLabel->setFont(GuardCodeFont(CODE_PIXEL_SIZE));
	codeLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	const QDateTime addedAt = record.source.importedAt.isValid() ? record.source.importedAt : record.createdAt;
	QStringList facts;
	if (addedAt.isValid()) {
		facts.append(tr("Added %1").arg(QLocale().toString(addedAt.toLocalTime().date(), QLocale::ShortFormat)));
	}
	facts.append(DetermineCapabilities(record).canSignConfirmations ? tr("Codes and confirmations") : tr("Codes only"));
	auto* factsLabel = new QLabel(facts.join(QLatin1Char('\n')), card);
	factsLabel->setObjectName(DETAIL_TEXT_STYLE);
	auto* cardLayout = new QVBoxLayout(card);
	cardLayout->setContentsMargins(CARD_MARGINS);
	cardLayout->setSpacing(CARD_SPACING);
	cardLayout->addWidget(titleLabel);
	cardLayout->addWidget(codeLabel);
	cardLayout->addWidget(factsLabel);
	return card;
}

QWidget* CloudConflictDialog::CreateChoiceRow(QWidget* parent, ConflictChoice choice, const QString& title, const QString& description) {
	auto* row = new QWidget(parent);
	auto* choiceButton = new QRadioButton(row);
	choiceButton->setAccessibleName(title);
	choiceGroup.addButton(choiceButton, static_cast<int>(choice));
	auto* titleLabel = new QLabel(title, row);
	auto* descriptionLabel = new QLabel(description, row);
	descriptionLabel->setObjectName(DETAIL_TEXT_STYLE);
	descriptionLabel->setWordWrap(true);
	auto* rowLayout = new QGridLayout(row);
	rowLayout->setContentsMargins(CHOICE_MARGINS);
	rowLayout->setHorizontalSpacing(CHOICE_HORIZONTAL_SPACING);
	rowLayout->setVerticalSpacing(CHOICE_VERTICAL_SPACING);
	rowLayout->addWidget(choiceButton, 0, 0, 2, 1, Qt::AlignVCenter);
	rowLayout->addWidget(titleLabel, 0, 1);
	rowLayout->addWidget(descriptionLabel, 1, 1);
	rowLayout->setColumnStretch(1, 1);
	// The labels leave their presses to the row.
	row->installEventFilter(this);
	choiceButtonsByRow.insert(row, choiceButton);
	return row;
}

void CloudConflictDialog::UpdateCodes() {
	const auto steamTime = std::chrono::floor<std::chrono::seconds>(steamClock.Now());
	localCodeLabel->setText(GenerateGuardCode(localSharedSecret, steamTime));
	cloudCodeLabel->setText(GenerateGuardCode(cloudSharedSecret, steamTime));
}

} // namespace SteamForge
