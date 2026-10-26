#pragma once

#include "steam/trade_offer_service.h"

namespace SteamForge {

class SteamImageCache;

inline constexpr int ITEM_TILE_EDGE = 60;
inline constexpr int ITEM_TILE_SPACING = 6;

// A tile of the Steam inventory: picture on the item background, rarity bar and quantity.
void PaintItemTile(QPainter& painter, const QFont& font, const TradeItem& item, const QImage& picture, const QRectF& tileRect, bool isHighlighted);

[[nodiscard]] QString ItemToolTip(const TradeItem& item);
[[nodiscard]] QString ItemDisplayName(const TradeItem& item);

// Empty tiles that stand for the items while they load, with a slow pulse.
class ItemTilePlaceholders final : public QWidget {
	Q_OBJECT

public:
	explicit ItemTilePlaceholders(QWidget* parent = nullptr);

	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	QVariantAnimation pulse;
};

// Every item of one side of a trade in rows of tiles; a click selects an item.
class ItemGrid final : public QWidget {
	Q_OBJECT

public:
	ItemGrid(SteamImageCache& images, std::vector<TradeItem> tradeItems, QWidget* parent = nullptr);

	void SetSelectedIndex(std::optional<std::size_t> itemIndex);

	[[nodiscard]] bool hasHeightForWidth() const override;
	[[nodiscard]] int heightForWidth(int width) const override;
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void ItemClicked(std::size_t itemIndex);

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	bool event(QEvent* event) override;

private:
	[[nodiscard]] static int ColumnCount(int width);
	[[nodiscard]] QRectF TileRect(std::size_t itemIndex) const;
	[[nodiscard]] std::optional<std::size_t> IndexAt(const QPointF& position) const;
	void HandleLoaded(const QUrl& loadedUrl);

	SteamImageCache& imageCache;
	std::vector<TradeItem> items;
	std::optional<std::size_t> selectedIndex;
};

} // namespace SteamForge
