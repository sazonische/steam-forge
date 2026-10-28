#include "ui/row_list.h"

#include "ui/widget_styles.h"

namespace SteamForge {

QFrame* CreateListFrame(QWidget* parent) {
	auto* listFrame = new QFrame(parent);
	listFrame->setObjectName(LIST_FRAME_STYLE);
	// Extra height of the window goes elsewhere: the frame stays around its rows, whatever the rows can take.
	listFrame->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);
	auto* frameLayout = new QVBoxLayout(listFrame);
	frameLayout->setContentsMargins(0, 0, 0, 0);
	frameLayout->setSpacing(0);
	return listFrame;
}

RowList::RowList(QWidget* parent) :
	QWidget(parent) {
	rowsLayout = new QVBoxLayout(this);
	rowsLayout->setContentsMargins(0, 0, 0, 0);
	rowsLayout->setSpacing(0);
	rowsLayout->setAlignment(Qt::AlignTop);
}

// Shown at once: rows added to an open window wait for a later show, and the height of the list would miss them.
void RowList::AddRow(QWidget* row) {
	if (rowsLayout->count() > 0) {
		QFrame* divider = CreateRowDivider(this);
		rowsLayout->addWidget(divider);
		divider->show();
	}
	rowsLayout->addWidget(row);
	row->show();
}

void RowList::Clear() {
	while (const QLayoutItem* item = rowsLayout->takeAt(0)) {
		if (QWidget* row = item->widget(); row != nullptr) {
			row->hide();
			row->deleteLater();
		}
		delete item;
	}
}

ScrollingRowList::ScrollingRowList(int maximumListHeight, QWidget* parent) :
	QScrollArea(parent),
	maximumHeight(maximumListHeight) {
	rowList = new RowList(this);
	setObjectName(DETAILS_SCROLL_STYLE);
	setFrameShape(QFrame::NoFrame);
	setWidgetResizable(true);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setMaximumHeight(maximumHeight);
	setWidget(rowList);
}

void ScrollingRowList::AddRow(QWidget* row) {
	rowList->AddRow(row);
	FitHeight();
}

void ScrollingRowList::Clear() {
	rowList->Clear();
	FitHeight();
}

void ScrollingRowList::FitHeight() {
	setMinimumHeight(std::min(rowList->sizeHint().height(), maximumHeight));
}

AccountChoiceRow CreateAccountChoiceRow(QWidget* picture, const QString& title, QWidget* parent) {
	auto* row = new QWidget(parent);
	auto* checkBox = new QCheckBox(row);
	checkBox->setAccessibleName(title);
	QSizePolicy checkBoxPolicy = checkBox->sizePolicy();
	checkBoxPolicy.setRetainSizeWhenHidden(true);
	checkBox->setSizePolicy(checkBoxPolicy);
	picture->setParent(row);
	// The name is chosen by the account owner and is never interpreted as rich text.
	auto* nameLabel = new QLabel(title, row);
	nameLabel->setTextFormat(Qt::PlainText);
	auto* rowLayout = new QHBoxLayout(row);
	rowLayout->setContentsMargins(LIST_ROW_MARGINS);
	rowLayout->setSpacing(LIST_ROW_SPACING);
	rowLayout->addWidget(checkBox);
	rowLayout->addWidget(picture);
	rowLayout->addWidget(nameLabel, 1);
	return {
		.row = row,
		.checkBox = checkBox,
		.layout = rowLayout,
	};
}

} // namespace SteamForge
