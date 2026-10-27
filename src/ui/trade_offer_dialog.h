#pragma once

#include "app/account_confirmations.h"

namespace SteamForge {

class ItemGrid;
class SteamImageCache;
class ItemTilePlaceholders;
class SteamPicture;

// One trade in full before it is decided: the partner, every item of both sides and links to check each item.
class TradeOfferDialog final : public QDialog {
	Q_OBJECT

public:
	TradeOfferDialog(AccountConfirmations& accountConfirmations, SteamImageCache& images, AuthenticatorRecord accountRecord, QString title, QString shownConfirmationId, QWidget* parent = nullptr);

protected:
	void changeEvent(QEvent* event) override;

private:
	enum class TradeSide {
		GIVE,
		RECEIVE
	};

	struct SideView {
		QLabel* titleLabel = nullptr;
		QLabel* emptyLabel = nullptr;
		ItemGrid* grid = nullptr;
		int itemCount = 0;
	};

	void RetranslateUi();
	void RetranslateSides();
	void UpdateState();
	void UpdatePartner();
	void ShowItems(const TradeOfferDetails& loadedOffer);
	[[nodiscard]] QWidget* CreateSide(TradeSide side, const std::vector<TradeItem>& items);
	void SelectItem(TradeSide side, std::size_t itemIndex);
	void ShowSelectedItem();
	void Respond(ConfirmationDecision decision);
	[[nodiscard]] SideView& ViewOf(TradeSide side);
	[[nodiscard]] const TradeItem* SelectedItem() const;
	[[nodiscard]] bool IsListed() const;
	[[nodiscard]] QString StatusText() const;

	QPointer<AccountConfirmations> confirmations;
	SteamImageCache& imageCache;
	AuthenticatorRecord record;
	// The Steam name, or the account name while Steam has told none.
	QString accountTitle;
	QString confirmationId;
	std::optional<PendingConfirmation> confirmation;
	std::optional<TradeOfferDetails> offer;
	std::optional<std::pair<TradeSide, std::size_t>> selection;
	bool wasDecisionSent = false;

	SteamPicture* partnerPicture = nullptr;
	QLabel* partnerNameLabel = nullptr;
	QLabel* partnerDetailsLabel = nullptr;
	QPushButton* profileButton = nullptr;
	QPushButton* steamOfferButton = nullptr;
	QLabel* holdLabel = nullptr;
	QLabel* warningsLabel = nullptr;
	QLabel* messageLabel = nullptr;
	QLabel* loadingLabel = nullptr;
	ItemTilePlaceholders* loadingTiles = nullptr;
	QWidget* sidesContainer = nullptr;
	QVBoxLayout* sidesLayout = nullptr;
	std::array<SideView, 2> sideViews{};

	QLabel* noSelectionLabel = nullptr;
	QScrollArea* itemScrollArea = nullptr;
	QWidget* itemPanel = nullptr;
	QLabel* itemSideLabel = nullptr;
	SteamPicture* itemPicture = nullptr;
	QLabel* itemNameLabel = nullptr;
	QLabel* itemTypeLabel = nullptr;
	QLabel* itemTagsLabel = nullptr;
	QLabel* itemWarningsLabel = nullptr;
	QLabel* itemFlagsLabel = nullptr;
	QWidget* itemActions = nullptr;
	QPushButton* inspectButton = nullptr;
	QPushButton* viewerButton = nullptr;
	QPushButton* marketButton = nullptr;
	QPushButton* inventoryButton = nullptr;

	QLabel* accountLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QPushButton* rejectButton = nullptr;
	QPushButton* acceptButton = nullptr;
	QPushButton* closeButton = nullptr;
};

} // namespace SteamForge
