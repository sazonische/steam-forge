#include "ui/success_dialog.h"

#include "ui/illustration_view.h"
#include "ui/image_resources.h"
#include "ui/theme.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int MINIMUM_DIALOG_WIDTH = 480;
	constexpr int DIALOG_SPACING = 12;
	constexpr int KEPT_VALUE_PIXEL_SIZE = 26;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins CARD_MARGINS{16, 12, 16, 12};
	constexpr QSize ILLUSTRATION_SIZE{360, 190};
} // namespace

SuccessDialog::SuccessDialog(const QString& headline, const QString& text, QWidget* parent) :
	QDialog(parent) {
	setWindowTitle(headline);
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);

	illustration = new IllustrationView(ALL_SET_ILLUSTRATION_RESOURCE, ILLUSTRATION_SIZE, this);
	headlineLabel = new QLabel(headline, this);
	headlineLabel->setObjectName(PAGE_TITLE_STYLE);
	headlineLabel->setAlignment(Qt::AlignCenter);
	headlineLabel->setWordWrap(true);
	textLabel = new QLabel(text, this);
	textLabel->setTextFormat(Qt::PlainText);
	textLabel->setAlignment(Qt::AlignHCenter);
	textLabel->setWordWrap(true);

	keptValueCard = new QFrame(this);
	keptValueCard->setObjectName(CARD_STYLE);
	keptValueTitleLabel = new QLabel(keptValueCard);
	MakeSectionLabel(*keptValueTitleLabel);
	keptValueLabel = new QLabel(keptValueCard);
	keptValueLabel->setTextFormat(Qt::PlainText);
	keptValueLabel->setFont(GuardCodeFont(KEPT_VALUE_PIXEL_SIZE));
	keptValueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	keptValueHintLabel = new QLabel(keptValueCard);
	keptValueHintLabel->setTextFormat(Qt::PlainText);
	keptValueHintLabel->setWordWrap(true);
	auto* keptValueLayout = new QVBoxLayout(keptValueCard);
	keptValueLayout->setContentsMargins(CARD_MARGINS);
	keptValueLayout->addWidget(keptValueTitleLabel);
	keptValueLayout->addWidget(keptValueLabel);
	keptValueLayout->addWidget(keptValueHintLabel);
	keptValueCard->hide();
	choiceCheckBox = new QCheckBox(this);
	choiceCheckBox->hide();

	doneButton = new QPushButton(tr("Done"), this);
	SetButtonRole(*doneButton, ButtonRole::PRIMARY);
	doneButton->setDefault(true);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(doneButton);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(illustration, 0, Qt::AlignHCenter);
	dialogLayout->addWidget(headlineLabel);
	dialogLayout->addWidget(textLabel);
	dialogLayout->addWidget(keptValueCard);
	dialogLayout->addWidget(choiceCheckBox);
	dialogLayout->addLayout(buttonsLayout);

	connect(doneButton, &QPushButton::clicked, this, &SuccessDialog::accept);
	FitHeightToContents(*this);
}

void SuccessDialog::SetKeptValue(const QString& title, const QString& value, const QString& hint) {
	keptValueTitleLabel->setText(title);
	keptValueLabel->setText(value);
	keptValueHintLabel->setText(hint);
	keptValueCard->setVisible(!value.isEmpty());
	FitHeightToContents(*this);
}

void SuccessDialog::SetChoice(const QString& text, bool isChecked) {
	choiceCheckBox->setText(text);
	choiceCheckBox->setChecked(isChecked);
	choiceCheckBox->show();
	FitHeightToContents(*this);
}

bool SuccessDialog::IsChoiceChecked() const {
	return !choiceCheckBox->isHidden() && choiceCheckBox->isChecked();
}

} // namespace SteamForge
