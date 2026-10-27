#include "ui/confirmation_card.h"

#include "steam/steam_image_cache.h"
#include "ui/item_tiles.h"
#include "ui/record_presentation.h"
#include "ui/steam_picture.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr int ICON_EDGE = 56;
	constexpr int OVERFLOW_WIDTH = 52;
	constexpr qreal CORNER_RADIUS = 3.0;
	constexpr qreal CARD_CORNER_RADIUS = 4.0;
	// A light veil over the card background, so the header stays lighter while the card is hovered or selected.
	constexpr QColor HEADER_BAND_COLOR{0xff, 0xff, 0xff, 12};
	constexpr qsizetype MAX_TOOLTIP_NAMES = 20;
	constexpr int ITEM_TITLE_SPACING = 4;
	constexpr qint64 SECONDS_PER_MINUTE = 60;
	constexpr qint64 MINUTES_PER_HOUR = 60;
	constexpr int ITEM_ROWS_SPACING = 10;
	constexpr int CARD_SPACING = 12;
	constexpr int CONTENT_SPACING = 6;
	constexpr QMargins CARD_MARGINS{14, 12, 14, 12};
	// The side padding lives in edge columns, so the divider alone reaches both edges of the card.
	constexpr int LEFT_EDGE_COLUMN = 0;
	constexpr int SELECTION_COLUMN = 1;
	constexpr int SELECTION_GAP_COLUMN = 2;
	constexpr int CONTENT_COLUMN = 3;
	constexpr int RIGHT_EDGE_COLUMN = 4;
	constexpr int ALL_COLUMNS_SPAN = RIGHT_EDGE_COLUMN + 1;
	constexpr QColor OVERFLOW_BACKGROUND{0x2b, 0x31, 0x3b};
	constexpr QColor OVERFLOW_TEXT_COLOR{0xff, 0xff, 0xff};
	constexpr int OVERFLOW_PIXEL_SIZE = 13;

	int StripWidth(std::size_t tileCount) {
		const auto count = static_cast<int>(tileCount);
		return (count * ITEM_TILE_EDGE) + (std::max(count - 1, 0) * ITEM_TILE_SPACING);
	}

	QLabel* CreatePlainLabel(QWidget* parent, QLatin1StringView styleName = {}) {
		auto* label = new QLabel(parent);
		// Steam supplies these texts, so they are never interpreted as rich text.
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		if (!styleName.isEmpty()) {
			label->setObjectName(styleName);
		}
		return label;
	}
} // namespace

ItemStrip::ItemStrip(SteamImageCache& images, std::vector<TradeItem> tradeItems, QWidget* parent) :
	QWidget(parent),
	imageCache(images),
	items(std::move(tradeItems)) {
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	setCursor(Qt::PointingHandCursor);
	QStringList names;
	for (const TradeItem& item : items | std::views::take(MAX_TOOLTIP_NAMES)) {
		names.append(ItemDisplayName(item));
	}
	setAccessibleName(names.join(QStringLiteral(", ")));
	connect(&imageCache, &SteamImageCache::Loaded, this, &ItemStrip::HandleLoaded);
}

QSize ItemStrip::sizeHint() const {
	return {StripWidth(items.size()), ITEM_TILE_EDGE};
}

QSize ItemStrip::minimumSizeHint() const {
	if (items.size() <= 1) {
		return sizeHint();
	}
	return {ITEM_TILE_EDGE + ITEM_TILE_SPACING + OVERFLOW_WIDTH, ITEM_TILE_EDGE};
}

void ItemStrip::paintEvent(QPaintEvent* /*event*/) {
	QPainter painter(this);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
	const std::size_t visibleCount = VisibleTileCount();
	for (std::size_t i = 0; i < visibleCount; ++i) {
		const TradeItem& item = items.at(i);
		const QImage picture = item.imageUrl.isEmpty() ? QImage() : imageCache.Image(item.imageUrl);
		PaintItemTile(painter, font(), item, picture, TileRect(i), false);
	}
	if (visibleCount == items.size()) {
		return;
	}
	const QRectF overflowRect = OverflowRect();
	QPainterPath frame;
	frame.addRoundedRect(overflowRect, CORNER_RADIUS, CORNER_RADIUS);
	painter.fillPath(frame, OVERFLOW_BACKGROUND);
	QFont overflowFont = font();
	overflowFont.setPixelSize(OVERFLOW_PIXEL_SIZE);
	overflowFont.setBold(true);
	painter.setFont(overflowFont);
	painter.setPen(OVERFLOW_TEXT_COLOR);
	painter.drawText(overflowRect, Qt::AlignCenter, QStringLiteral("+%1").arg(items.size() - visibleCount));
}

void ItemStrip::mousePressEvent(QMouseEvent* event) {
	if (event->button() == Qt::LeftButton) {
		Q_EMIT Clicked();
		return;
	}
	QWidget::mousePressEvent(event);
}

bool ItemStrip::event(QEvent* event) {
	if (event->type() != QEvent::ToolTip) {
		return QWidget::event(event);
	}
	const auto* helpEvent = static_cast<QHelpEvent*>(event);
	const QPointF position = helpEvent->pos();
	const std::size_t visibleCount = VisibleTileCount();
	for (std::size_t i = 0; i < visibleCount; ++i) {
		if (TileRect(i).contains(position)) {
			QToolTip::showText(helpEvent->globalPos(), ItemToolTip(items.at(i)), this);
			return true;
		}
	}
	if (visibleCount < items.size() && OverflowRect().contains(position)) {
		QToolTip::showText(helpEvent->globalPos(), OverflowToolTip(), this);
		return true;
	}
	QToolTip::hideText();
	event->ignore();
	return true;
}

std::size_t ItemStrip::VisibleTileCount() const {
	if (StripWidth(items.size()) <= width()) {
		return items.size();
	}
	const int tilesWidth = width() - OVERFLOW_WIDTH;
	const int fittingTiles = std::max(tilesWidth / (ITEM_TILE_EDGE + ITEM_TILE_SPACING), 0);
	return std::min(static_cast<std::size_t>(fittingTiles), items.size() - 1);
}

QRectF ItemStrip::TileRect(std::size_t tileIndex) {
	return {static_cast<qreal>(static_cast<int>(tileIndex) * (ITEM_TILE_EDGE + ITEM_TILE_SPACING)), 0, ITEM_TILE_EDGE, ITEM_TILE_EDGE};
}

QRectF ItemStrip::OverflowRect() const {
	return {static_cast<qreal>(static_cast<int>(VisibleTileCount()) * (ITEM_TILE_EDGE + ITEM_TILE_SPACING)), 0, OVERFLOW_WIDTH, ITEM_TILE_EDGE};
}

QString ItemStrip::OverflowToolTip() const {
	const std::size_t hiddenStart = VisibleTileCount();
	QStringList names;
	for (const TradeItem& item : items | std::views::drop(hiddenStart) | std::views::take(MAX_TOOLTIP_NAMES)) {
		names.append(item.amount > 1 ? QStringLiteral("%1 ×%2").arg(ItemDisplayName(item)).arg(item.amount) : ItemDisplayName(item));
	}
	const auto remainingCount = static_cast<qsizetype>(items.size() - hiddenStart) - MAX_TOOLTIP_NAMES;
	if (remainingCount > 0) {
		names.append(tr("and %n more", nullptr, static_cast<int>(remainingCount)));
	}
	names.append(tr("Click to see the whole trade."));
	return names.join(QLatin1Char('\n'));
}

void ItemStrip::HandleLoaded(const QUrl& loadedUrl) {
	if (std::ranges::find(items, loadedUrl, &TradeItem::imageUrl) != items.end()) {
		update();
	}
}

ConfirmationCard::ConfirmationCard(SteamImageCache& images, QWidget* parent) :
	QFrame(parent),
	imageCache(images) {
	setObjectName(CONFIRMATION_CARD_STYLE);
	selectionBox = new QCheckBox(this);
	iconPicture = new SteamPicture(imageCache, ICON_EDGE, this);
	typeLabel = CreatePlainLabel(this);
	MakeSectionLabel(*typeLabel);
	timeLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	timeLabel->setWordWrap(false);
	headlineLabel = CreatePlainLabel(this, ROW_NAME_STYLE);
	headlineLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	itemsWidget = new QWidget(this);
	itemsLayout = new QVBoxLayout(itemsWidget);
	itemsLayout->setContentsMargins(0, 0, 0, 0);
	itemsLayout->setSpacing(ITEM_ROWS_SPACING);
	summaryLabel = CreatePlainLabel(this);
	summaryLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	messageLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	warningsLabel = CreatePlainLabel(this, ERROR_TEXT_STYLE);
	sendingLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	// A link, not a third button: the decision keeps the only buttons, and the items open the trade as well.
	detailsButton = new QPushButton(this);
	detailsButton->setObjectName(LINK_BUTTON_STYLE);
	detailsButton->setCursor(Qt::PointingHandCursor);
	rejectButton = new QPushButton(this);
	acceptButton = new QPushButton(this);
	SetButtonRole(*acceptButton, ButtonRole::PRIMARY);

	auto* headerLayout = new QHBoxLayout();
	headerLayout->addWidget(typeLabel, 1);
	headerLayout->addWidget(timeLabel);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addWidget(sendingLabel, 1);
	buttonsLayout->addWidget(detailsButton);
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(rejectButton);
	buttonsLayout->addWidget(acceptButton);
	auto* titleLayout = new QVBoxLayout();
	titleLayout->setSpacing(CONTENT_SPACING);
	titleLayout->addStretch();
	titleLayout->addLayout(headerLayout);
	titleLayout->addWidget(headlineLabel);
	titleLayout->addStretch();
	auto* bodyLayout = new QVBoxLayout();
	bodyLayout->setSpacing(CONTENT_SPACING);
	bodyLayout->addWidget(itemsWidget);
	bodyLayout->addWidget(summaryLabel);
	bodyLayout->addWidget(messageLabel);
	bodyLayout->addWidget(warningsLabel);
	bodyLayout->addLayout(buttonsLayout);
	cardLayout = new QGridLayout(this);
	cardLayout->setContentsMargins(0, CARD_MARGINS.top(), 0, CARD_MARGINS.bottom());
	cardLayout->setHorizontalSpacing(0);
	cardLayout->setVerticalSpacing(CARD_SPACING);
	cardLayout->setColumnMinimumWidth(LEFT_EDGE_COLUMN, CARD_MARGINS.left());
	cardLayout->setColumnMinimumWidth(SELECTION_GAP_COLUMN, CARD_SPACING);
	cardLayout->setColumnMinimumWidth(RIGHT_EDGE_COLUMN, CARD_MARGINS.right());
	// The picture shares the cell of the title, so a card without one keeps the title in line with the body.
	auto* headLayout = new QHBoxLayout();
	headLayout->setSpacing(CARD_SPACING);
	headLayout->addWidget(iconPicture, 0, Qt::AlignTop);
	headLayout->addLayout(titleLayout, 1);
	// Where the lighter header band ends: who and what stays apart from the items and the decision.
	headerEdge = new QWidget(this);
	headerEdge->setFixedHeight(1);
	cardLayout->addWidget(selectionBox, 0, SELECTION_COLUMN, Qt::AlignVCenter);
	cardLayout->addLayout(headLayout, 0, CONTENT_COLUMN);
	cardLayout->addWidget(headerEdge, 1, LEFT_EDGE_COLUMN, 1, ALL_COLUMNS_SPAN);
	cardLayout->addLayout(bodyLayout, 2, CONTENT_COLUMN);
	cardLayout->setColumnStretch(CONTENT_COLUMN, 1);

	connect(selectionBox, &QCheckBox::toggled, this, [this](bool isChecked) {
		Q_EMIT SelectionToggled(confirmation.id, isChecked);
	});
	connect(detailsButton, &QPushButton::clicked, this, [this]() {
		Q_EMIT TradeDetailsRequested(confirmation.id);
	});
	connect(acceptButton, &QPushButton::clicked, this, [this]() {
		Q_EMIT DecisionRequested(confirmation.id, ConfirmationDecision::ACCEPT);
	});
	connect(rejectButton, &QPushButton::clicked, this, [this]() {
		Q_EMIT DecisionRequested(confirmation.id, ConfirmationDecision::REJECT);
	});
	RetranslateUi();
}

void ConfirmationCard::Show(const PendingConfirmation& shownConfirmation, const TradeOfferDetails* tradeOffer, const ConfirmationCardState& state) {
	confirmation = shownConfirmation;
	iconPicture->SetUrl(confirmation.iconUrl);
	// An empty square says nothing: without a picture the title starts at the edge.
	iconPicture->setVisible(!confirmation.iconUrl.isEmpty());
	typeLabel->setText(ConfirmationTypeText(confirmation));
	const QDateTime createdAt = confirmation.createdAt.toLocalTime();
	timeLabel->setText(TimeText(createdAt));
	timeLabel->setToolTip(createdAt.isValid() ? QLocale().toString(createdAt, QLocale::LongFormat) : QString());
	headlineLabel->setText(confirmation.headline);
	headlineLabel->setVisible(!confirmation.headline.isEmpty());
	ShowItems(tradeOffer);
	warningsLabel->setText(confirmation.warnings.join(QLatin1Char('\n')));
	warningsLabel->setVisible(!confirmation.warnings.isEmpty());

	const QSignalBlocker selectionBlocker(selectionBox);
	selectionBox->setVisible(confirmation.allowsBatchResponse);
	cardLayout->setColumnMinimumWidth(SELECTION_GAP_COLUMN, confirmation.allowsBatchResponse ? CARD_SPACING : 0);
	selectionBox->setChecked(state.isSelected);
	selectionBox->setEnabled(state.canDecide);
	SetStyleFlag(*this, StyleFlag::SELECTED, state.isSelected);
	sendingLabel->setVisible(state.isResponding);
	detailsButton->setVisible(confirmation.category == ConfirmationCategory::TRADE && !confirmation.creatorId.isEmpty());
	acceptButton->setVisible(!state.isResponding);
	rejectButton->setVisible(!state.isResponding);
	acceptButton->setEnabled(state.canDecide);
	rejectButton->setEnabled(state.canDecide);
	RetranslateUi();
}

const PendingConfirmation& ConfirmationCard::Confirmation() const {
	return confirmation;
}

void ConfirmationCard::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
		shownOfferId.reset();
	}
	QFrame::changeEvent(event);
}

// As people say it: "just now", "5 min ago", "today, 14:05", "yesterday, 23:50"; the full date is in the tooltip.
QString ConfirmationCard::TimeText(const QDateTime& createdAt) {
	if (!createdAt.isValid()) {
		return {};
	}
	const QDateTime now = QDateTime::currentDateTime();
	const qint64 minutesAgo = createdAt.secsTo(now) / SECONDS_PER_MINUTE;
	const QString time = QLocale().toString(createdAt.time(), QLocale::ShortFormat);
	if (minutesAgo < 1) {
		return tr("just now");
	}
	if (minutesAgo < MINUTES_PER_HOUR) {
		return tr("%1 min ago").arg(minutesAgo);
	}
	if (createdAt.date() == now.date()) {
		return tr("today, %1").arg(time);
	}
	if (createdAt.date() == now.date().addDays(-1)) {
		return tr("yesterday, %1").arg(time);
	}
	return QLocale().toString(createdAt, QLocale::ShortFormat);
}

void ConfirmationCard::paintEvent(QPaintEvent* event) {
	QFrame::paintEvent(event);
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	// Inside the one-pixel border, which shows the selection.
	const QRectF inside = QRectF(rect()).adjusted(1, 1, -1, -1);
	QPainterPath card;
	card.addRoundedRect(inside, CARD_CORNER_RADIUS, CARD_CORNER_RADIUS);
	painter.setClipPath(card);
	painter.fillRect(QRectF(inside.left(), inside.top(), inside.width(), headerEdge->geometry().top() - inside.top()), HEADER_BAND_COLOR);
}

void ConfirmationCard::RetranslateUi() {
	detailsButton->setText(tr("Trade details…"));
	acceptButton->setText(tr("Confirm"));
	rejectButton->setText(tr("Decline"));
	sendingLabel->setText(tr("Sending the decision to Steam…"));
	selectionBox->setAccessibleName(tr("Select %1").arg(confirmation.headline));
}

void ConfirmationCard::ShowItems(const TradeOfferDetails* tradeOffer) {
	const std::optional<QString> offerId = tradeOffer != nullptr ? std::optional<QString>(tradeOffer->offerId) : std::nullopt;
	// Rebuilt only when the offer appears: the tiles keep their pictures through the periodic list updates.
	if (offerId != shownOfferId) {
		shownOfferId = offerId;
		while (const QLayoutItem* row = itemsLayout->takeAt(0)) {
			row->widget()->deleteLater();
			delete row;
		}
		if (tradeOffer != nullptr) {
			itemsLayout->addWidget(CreateItemRow(true, tradeOffer->itemsToGive));
			itemsLayout->addWidget(CreateItemRow(false, tradeOffer->itemsToReceive));
		}
	}
	itemsWidget->setVisible(tradeOffer != nullptr);
	// Steam's own text stays as long as the items themselves are unknown.
	summaryLabel->setText(confirmation.summary.join(QLatin1Char('\n')));
	summaryLabel->setVisible(tradeOffer == nullptr && !confirmation.summary.isEmpty());
	const QString message = tradeOffer != nullptr ? tradeOffer->message.trimmed() : QString();
	messageLabel->setText(tr("Message: %1").arg(message));
	messageLabel->setVisible(!message.isEmpty());
}

// The side as a caption above its items, which then use the whole width of the card.
QWidget* ConfirmationCard::CreateItemRow(bool isGiven, const std::vector<TradeItem>& items) {
	auto* row = new QWidget(itemsWidget);
	auto* titleLabel = CreatePlainLabel(row, SECONDARY_TEXT_STYLE);
	auto* rowLayout = new QVBoxLayout(row);
	rowLayout->setContentsMargins(0, 0, 0, 0);
	rowLayout->setSpacing(ITEM_TITLE_SPACING);
	rowLayout->addWidget(titleLabel);
	if (items.empty()) {
		titleLabel->setText(tr("%1: %2").arg(isGiven ? tr("You give") : tr("You receive"), tr("nothing")));
		return row;
	}
	// The count tells how many hide behind "+N".
	const auto itemCount = static_cast<int>(items.size());
	titleLabel->setText(isGiven ? tr("You give · %n item(s)", nullptr, itemCount) : tr("You receive · %n item(s)", nullptr, itemCount));
	auto* strip = new ItemStrip(imageCache, items, row);
	connect(strip, &ItemStrip::Clicked, this, [this]() {
		Q_EMIT TradeDetailsRequested(confirmation.id);
	});
	rowLayout->addWidget(strip);
	return row;
}

} // namespace SteamForge
