#pragma once

#include "app/account_confirmations.h"

namespace SteamForge {

class ConfirmationCard;
class ConfirmationMonitor;
class IllustrationView;
class SteamImageCache;
class TradeOfferDialog;

// The mobile confirmations of one account; the account is fixed when the window opens.
class ConfirmationsDialog final : public QDialog {
	Q_OBJECT

public:
	ConfirmationsDialog(ConfirmationMonitor& monitor, SteamImageCache& images, AuthenticatorRecord accountRecord, QString title, QWidget* parent = nullptr);

	[[nodiscard]] const QUuid& RecordId() const;

Q_SIGNALS:
	void SignInRequested(const QUuid& recordId);

protected:
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	enum class EntryFilter {
		ALL,
		TRADES,
		MARKET_LISTINGS,
		OTHER
	};

	void Bind();
	void RetranslateUi();
	void UpdateState();
	void SynchronizeCards(bool canDecide);
	void UpdateEmptyState(bool isSignInRequired);
	void UpdateFilters();
	void SelectFilter(EntryFilter newFilter);
	void UpdateSelectionButtons();
	void ToggleSelectAll();
	void Respond(const QString& confirmationId, ConfirmationDecision decision);
	void RespondToSelected(ConfirmationDecision decision);
	void OpenTradeOffer(const QString& confirmationId);
	void CloseTradeOffers();
	[[nodiscard]] bool CanDecide() const;
	[[nodiscard]] bool IsShown(const PendingConfirmation& confirmation) const;
	[[nodiscard]] QStringList SelectableIds() const;
	[[nodiscard]] QStringList SelectedIds() const;
	[[nodiscard]] QString StatusText() const;
	[[nodiscard]] QString ResponseFailureText() const;

	ConfirmationMonitor& confirmationMonitor;
	SteamImageCache& imageCache;
	AuthenticatorRecord record;
	// The Steam name, or the account name while Steam has told none.
	QString accountTitle;
	QPointer<AccountConfirmations> confirmations;
	QHash<QString, ConfirmationCard*> cards;
	QHash<QString, QPointer<TradeOfferDialog>> tradeOfferDialogs;
	QSet<QString> selectedIds;
	EntryFilter filter = EntryFilter::ALL;
	bool hasRefreshedOnOpen = false;

	QLabel* accountNameLabel = nullptr;
	QLabel* accountDetailsLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QLabel* responseFailureLabel = nullptr;
	QWidget* filterBar = nullptr;
	QButtonGroup filterGroup;
	std::array<QPushButton*, 4> filterButtons{};
	QWidget* entriesContainer = nullptr;
	QVBoxLayout* entriesLayout = nullptr;
	QWidget* emptyState = nullptr;
	IllustrationView* emptyIllustration = nullptr;
	QLabel* emptyTitleLabel = nullptr;
	QLabel* emptyLabel = nullptr;
	QPushButton* selectAllButton = nullptr;
	QPushButton* acceptSelectedButton = nullptr;
	QPushButton* rejectSelectedButton = nullptr;
	QPushButton* refreshButton = nullptr;
	QPushButton* signInButton = nullptr;
	QPushButton* closeButton = nullptr;
};

} // namespace SteamForge
