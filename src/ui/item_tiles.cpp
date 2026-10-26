#include "ui/item_tiles.h"

#include "steam/steam_image_cache.h"
#include "ui/steam_picture.h"

namespace SteamForge {

namespace {
	constexpr int TILE_PADDING = 5;
	constexpr int RARITY_BAR_HEIGHT = 3;
	constexpr qreal CORNER_RADIUS = 3.0;
	constexpr qreal HIGHLIGHT_WIDTH = 2.0;
	constexpr int AMOUNT_PIXEL_SIZE = 11;
	constexpr int GRID_PREFERRED_COLUMNS = 6;
	constexpr QColor TILE_BACKGROUND{0x2b, 0x31, 0x3b};
	constexpr QColor DEFAULT_RARITY_COLOR{0x4b, 0x55, 0x63};
	constexpr QColor HIGHLIGHT_COLOR{0x1a, 0x9f, 0xff};
	constexpr QColor LABEL_COLOR{0xff, 0xff, 0xff};
	constexpr QColor LABEL_SHADOW_COLOR{0x00, 0x00, 0x00, 0xb4};
	constexpr int PLACEHOLDER_ROWS = 2;
	constexpr int PLACEHOLDER_PREFERRED_COLUMNS = 6;
	constexpr qreal PLACEHOLDER_DIM_OPACITY = 0.55;
	constexpr qreal PLACEHOLDER_BRIGHT_OPACITY = 1.0;
	constexpr qreal PULSE_START = 0.0;
	constexpr qreal PULSE_MIDDLE = 0.5;
	constexpr qreal PULSE_END = 1.0;
	constexpr int ENDLESS_LOOP = -1;
	constexpr std::chrono::milliseconds PULSE_DURATION{1600};
} // namespace

void PaintItemTile(QPainter& painter, const QFont& font, const TradeItem& item, const QImage& picture, const QRectF& tileRect, bool isHighlighted) {
	painter.save();
	painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);
	QPainterPath frame;
	frame.addRoundedRect(tileRect, CORNER_RADIUS, CORNER_RADIUS);
	painter.fillPath(frame, item.backgroundColor.isValid() ? item.backgroundColor : TILE_BACKGROUND);
	painter.setClipPath(frame);
	const QRectF contentRect = tileRect.adjusted(TILE_PADDING, TILE_PADDING, -TILE_PADDING, -TILE_PADDING - RARITY_BAR_HEIGHT);
	if (!picture.isNull()) {
		painter.drawImage(FittedRect(picture.size(), contentRect), picture);
	}
	painter.fillRect(QRectF(tileRect.left(), tileRect.bottom() - RARITY_BAR_HEIGHT, tileRect.width(), RARITY_BAR_HEIGHT), item.nameColor.isValid() ? item.nameColor : DEFAULT_RARITY_COLOR);
	if (item.amount > 1) {
		QFont amountFont = font;
		amountFont.setPixelSize(AMOUNT_PIXEL_SIZE);
		amountFont.setBold(true);
		painter.setFont(amountFont);
		const QString amountText = QStringLiteral("×%1").arg(item.amount);
		painter.setPen(LABEL_SHADOW_COLOR);
		painter.drawText(contentRect.translated(1, 1), Qt::AlignRight | Qt::AlignBottom, amountText);
		painter.setPen(LABEL_COLOR);
		painter.drawText(contentRect, Qt::AlignRight | Qt::AlignBottom, amountText);
	}
	if (isHighlighted) {
		painter.setClipping(false);
		painter.setPen(QPen(HIGHLIGHT_COLOR, HIGHLIGHT_WIDTH));
		painter.setBrush(Qt::NoBrush);
		const qreal inset = HIGHLIGHT_WIDTH / 2;
		painter.drawRoundedRect(tileRect.adjusted(inset, inset, -inset, -inset), CORNER_RADIUS, CORNER_RADIUS);
	}
	painter.restore();
}

QString ItemDisplayName(const TradeItem& item) {
	return item.name.isEmpty() ? QObject::tr("Item without a description") : item.name;
}

QString ItemToolTip(const TradeItem& item) {
	QStringList lines{ItemDisplayName(item)};
	if (!item.type.isEmpty()) {
		lines.append(item.type);
	}
	if (item.amount > 1) {
		lines.append(QObject::tr("Quantity: %1").arg(item.amount));
	}
	return lines.join(QLatin1Char('\n'));
}

ItemGrid::ItemGrid(SteamImageCache& images, std::vector<TradeItem> tradeItems, QWidget* parent) :
	QWidget(parent),
	imageCache(images),
	items(std::move(tradeItems)) {
	QSizePolicy sizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
	sizePolicy.setHeightForWidth(true);
	setSizePolicy(sizePolicy);
	setCursor(Qt::PointingHandCursor);
	connect(&imageCache, &SteamImageCache::Loaded, this, &ItemGrid::HandleLoaded);
}

void ItemGrid::SetSelectedIndex(std::optional<std::size_t> itemIndex) {
	if (selectedIndex != itemIndex) {
		selectedIndex = itemIndex;
		update();
	}
}

bool ItemGrid::hasHeightForWidth() const {
	return true;
}

int ItemGrid::heightForWidth(int width) const {
	const auto count = static_cast<int>(items.size());
	const int columns = ColumnCount(width);
	const int rows = (count + columns - 1) / columns;
	return (std::max(rows, 1) * (ITEM_TILE_EDGE + ITEM_TILE_SPACING)) - ITEM_TILE_SPACING;
}

QSize ItemGrid::sizeHint() const {
	const int width = (GRID_PREFERRED_COLUMNS * (ITEM_TILE_EDGE + ITEM_TILE_SPACING)) - ITEM_TILE_SPACING;
	return {width, heightForWidth(width)};
}

QSize ItemGrid::minimumSizeHint() const {
	return {ITEM_TILE_EDGE, ITEM_TILE_EDGE};
}

void ItemGrid::paintEvent(QPaintEvent* /*event*/) {
	QPainter painter(this);
	for (std::size_t i = 0; i < items.size(); ++i) {
		const TradeItem& item = items.at(i);
		const QImage picture = item.imageUrl.isEmpty() ? QImage() : imageCache.Image(item.imageUrl);
		PaintItemTile(painter, font(), item, picture, TileRect(i), selectedIndex == i);
	}
}

void ItemGrid::mousePressEvent(QMouseEvent* event) {
	if (event->button() == Qt::LeftButton) {
		if (const std::optional<std::size_t> itemIndex = IndexAt(event->position()); itemIndex.has_value()) {
			Q_EMIT ItemClicked(*itemIndex);
			return;
		}
	}
	QWidget::mousePressEvent(event);
}

bool ItemGrid::event(QEvent* event) {
	if (event->type() != QEvent::ToolTip) {
		return QWidget::event(event);
	}
	const auto* helpEvent = static_cast<QHelpEvent*>(event);
	if (const std::optional<std::size_t> itemIndex = IndexAt(helpEvent->pos()); itemIndex.has_value()) {
		QToolTip::showText(helpEvent->globalPos(), ItemToolTip(items.at(*itemIndex)), this);
	} else {
		QToolTip::hideText();
		event->ignore();
	}
	return true;
}

ItemTilePlaceholders::ItemTilePlaceholders(QWidget* parent) :
	QWidget(parent) {
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	pulse.setDuration(static_cast<int>(PULSE_DURATION.count()));
	pulse.setLoopCount(ENDLESS_LOOP);
	pulse.setEasingCurve(QEasingCurve::InOutSine);
	pulse.setKeyValueAt(PULSE_START, PLACEHOLDER_DIM_OPACITY);
	pulse.setKeyValueAt(PULSE_MIDDLE, PLACEHOLDER_BRIGHT_OPACITY);
	pulse.setKeyValueAt(PULSE_END, PLACEHOLDER_DIM_OPACITY);
	connect(&pulse, &QVariantAnimation::valueChanged, this, [this]() {
		update();
	});
}

QSize ItemTilePlaceholders::sizeHint() const {
	return {(PLACEHOLDER_PREFERRED_COLUMNS * ITEM_TILE_EDGE) + ((PLACEHOLDER_PREFERRED_COLUMNS - 1) * ITEM_TILE_SPACING), (PLACEHOLDER_ROWS * ITEM_TILE_EDGE) + ((PLACEHOLDER_ROWS - 1) * ITEM_TILE_SPACING)};
}

void ItemTilePlaceholders::paintEvent(QPaintEvent* /*event*/) {
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setOpacity(pulse.state() == QAbstractAnimation::Running ? pulse.currentValue().toReal() : PLACEHOLDER_DIM_OPACITY);
	const int columns = std::max((width() + ITEM_TILE_SPACING) / (ITEM_TILE_EDGE + ITEM_TILE_SPACING), 1);
	for (int row = 0; row < PLACEHOLDER_ROWS; ++row) {
		for (int column = 0; column < columns; ++column) {
			QPainterPath tile;
			tile.addRoundedRect(QRectF(column * (ITEM_TILE_EDGE + ITEM_TILE_SPACING), row * (ITEM_TILE_EDGE + ITEM_TILE_SPACING), ITEM_TILE_EDGE, ITEM_TILE_EDGE), CORNER_RADIUS, CORNER_RADIUS);
			painter.fillPath(tile, TILE_BACKGROUND);
		}
	}
}

void ItemTilePlaceholders::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	pulse.start();
}

void ItemTilePlaceholders::hideEvent(QHideEvent* event) {
	pulse.stop();
	QWidget::hideEvent(event);
}

int ItemGrid::ColumnCount(int width) {
	return std::max((width + ITEM_TILE_SPACING) / (ITEM_TILE_EDGE + ITEM_TILE_SPACING), 1);
}

QRectF ItemGrid::TileRect(std::size_t itemIndex) const {
	const auto columns = static_cast<std::size_t>(ColumnCount(width()));
	const auto column = static_cast<int>(itemIndex % columns);
	const auto row = static_cast<int>(itemIndex / columns);
	return {static_cast<qreal>(column * (ITEM_TILE_EDGE + ITEM_TILE_SPACING)), static_cast<qreal>(row * (ITEM_TILE_EDGE + ITEM_TILE_SPACING)), ITEM_TILE_EDGE, ITEM_TILE_EDGE};
}

std::optional<std::size_t> ItemGrid::IndexAt(const QPointF& position) const {
	for (std::size_t i = 0; i < items.size(); ++i) {
		if (TileRect(i).contains(position)) {
			return i;
		}
	}
	return std::nullopt;
}

void ItemGrid::HandleLoaded(const QUrl& loadedUrl) {
	if (std::ranges::find(items, loadedUrl, &TradeItem::imageUrl) != items.end()) {
		update();
	}
}

} // namespace SteamForge
