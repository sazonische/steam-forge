#pragma once

#include "steam/confirmation_service.h"
#include "steam/trade_offer_service.h"

namespace SteamForge {

class SteamImageCache;
class SteamPicture;

// The items of one side of a trade in one row. As many tiles as fit the width are drawn and the rest is
// counted, so a large trade never widens the window; the trade window shows all of them.
class ItemStrip final : public QWidget {
	Q_OBJECT

public:
	ItemStrip(SteamImageCache& images, std::vector<TradeItem> tradeItems, QWidget* parent = nullptr);

	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

Q_SIGNALS:
	void Clicked();

protected:
	void paintEvent(QPaintEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	bool event(QEvent* event) override;

private:
	[[nodiscard]] std::size_t VisibleTileCount() const;
	[[nodiscard]] static QRectF TileRect(std::size_t tileIndex);
	[[nodiscard]] QRectF OverflowRect() const;
	[[nodiscard]] QString OverflowToolTip() const;
	void HandleLoaded(const QUrl& loadedUrl);

	SteamImageCache& imageCache;
	std::vector<TradeItem> items;
};

struct ConfirmationCardState {
	bool canDecide = false;
	bool isResponding = false;
	bool isSelected = false;
};

class ConfirmationCard final : public QFrame {
	Q_OBJECT

public:
	ConfirmationCard(SteamImageCache& images, QWidget* parent = nullptr);

	void Show(const PendingConfirmation& shownConfirmation, const TradeOfferDetails* tradeOffer, const ConfirmationCardState& state);
	[[nodiscard]] const PendingConfirmation& Confirmation() const;

Q_SIGNALS:
	void DecisionRequested(const QString& confirmationId, ConfirmationDecision decision);
	void SelectionToggled(const QString& confirmationId, bool isSelected);
	void TradeDetailsRequested(const QString& confirmationId);

protected:
	void changeEvent(QEvent* event) override;
	void paintEvent(QPaintEvent* event) override;

private:
	void RetranslateUi();
	void ShowItems(const TradeOfferDetails* tradeOffer);
	[[nodiscard]] static QString TimeText(const QDateTime& createdAt);
	[[nodiscard]] QWidget* CreateItemRow(bool isGiven, const std::vector<TradeItem>& items);

	SteamImageCache& imageCache;
	PendingConfirmation confirmation;
	std::optional<QString> shownOfferId;

	QGridLayout* cardLayout = nullptr;
	QWidget* headerEdge = nullptr;
	QCheckBox* selectionBox = nullptr;
	SteamPicture* iconPicture = nullptr;
	QLabel* typeLabel = nullptr;
	QLabel* timeLabel = nullptr;
	QLabel* headlineLabel = nullptr;
	QWidget* itemsWidget = nullptr;
	QVBoxLayout* itemsLayout = nullptr;
	QLabel* summaryLabel = nullptr;
	QLabel* messageLabel = nullptr;
	QLabel* warningsLabel = nullptr;
	QLabel* sendingLabel = nullptr;
	QPushButton* detailsButton = nullptr;
	QPushButton* rejectButton = nullptr;
	QPushButton* acceptButton = nullptr;
};

} // namespace SteamForge
