#include "ui/trade_offer_dialog.h"

#include "steam/steam_image_cache.h"
#include "steam/steam_links.h"
#include "ui/confirmation_card.h"
#include "ui/item_tiles.h"
#include "ui/record_presentation.h"
#include "ui/steam_picture.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr QSize INITIAL_DIALOG_SIZE{1000, 800};
	constexpr QSize MINIMUM_DIALOG_SIZE{820, 620};
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins PANEL_MARGINS{16, 16, 16, 16};
	constexpr int DIALOG_SPACING = 12;
	constexpr int SECTION_SPACING = 8;
	constexpr int PARTNER_PICTURE_EDGE = 64;
	constexpr int ITEM_PICTURE_EDGE = 200;
	constexpr int ITEM_PANEL_WIDTH = 300;

	QLabel* CreatePlainLabel(QWidget* parent, QLatin1StringView styleName = {}) {
		auto* label = new QLabel(parent);
		// Steam and the partner supply these texts, so they are never interpreted as rich text.
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		if (!styleName.isEmpty()) {
			label->setObjectName(styleName);
		}
		return label;
	}

	void OpenLink(const QUrl& url) {
		if (url.isValid()) {
			QDesktopServices::openUrl(url);
		}
	}

	QString FormatTime(const QDateTime& time) {
		return QLocale().toString(time.toLocalTime(), QLocale::ShortFormat);
	}
} // namespace

TradeOfferDialog::TradeOfferDialog(AccountConfirmations& accountConfirmations, SteamImageCache& images, AuthenticatorRecord accountRecord, QString title, QString shownConfirmationId, QWidget* parent) :
	QDialog(parent),
	confirmations(&accountConfirmations),
	imageCache(images),
	record(std::move(accountRecord)),
	accountTitle(std::move(title)),
	confirmationId(std::move(shownConfirmationId)) {
	resize(INITIAL_DIALOG_SIZE);
	setMinimumSize(MINIMUM_DIALOG_SIZE);

	partnerPicture = new SteamPicture(imageCache, PARTNER_PICTURE_EDGE, this);
	partnerNameLabel = CreatePlainLabel(this, PAGE_TITLE_STYLE);
	partnerDetailsLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	partnerDetailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	profileButton = new QPushButton(this);
	steamOfferButton = new QPushButton(this);
	auto* partnerButtonsLayout = new QHBoxLayout();
	partnerButtonsLayout->addWidget(profileButton);
	partnerButtonsLayout->addWidget(steamOfferButton);
	partnerButtonsLayout->addStretch();
	auto* partnerTextLayout = new QVBoxLayout();
	partnerTextLayout->addWidget(partnerNameLabel);
	partnerTextLayout->addWidget(partnerDetailsLabel);
	partnerTextLayout->addLayout(partnerButtonsLayout);
	auto* headerLayout = new QHBoxLayout();
	headerLayout->setSpacing(DIALOG_SPACING);
	headerLayout->addWidget(partnerPicture, 0, Qt::AlignTop);
	headerLayout->addLayout(partnerTextLayout, 1);

	holdLabel = CreatePlainLabel(this, NOTICE_STYLE);
	warningsLabel = CreatePlainLabel(this, ERROR_TEXT_STYLE);
	messageLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	messageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

	sidesContainer = new QWidget(this);
	sidesContainer->setObjectName(DETAILS_PAGE_STYLE);
	sidesLayout = new QVBoxLayout(sidesContainer);
	sidesLayout->setContentsMargins(0, 0, 0, 0);
	sidesLayout->setSpacing(SECTION_SPACING);
	loadingLabel = CreatePlainLabel(sidesContainer, SECONDARY_TEXT_STYLE);
	sidesLayout->addWidget(loadingLabel);
	loadingTiles = new ItemTilePlaceholders(sidesContainer);
	sidesLayout->addWidget(loadingTiles);
	sidesLayout->addStretch();
	auto* sidesScrollArea = new QScrollArea(this);
	sidesScrollArea->setObjectName(DETAILS_SCROLL_STYLE);
	sidesScrollArea->setFrameShape(QFrame::NoFrame);
	sidesScrollArea->setWidgetResizable(true);
	sidesScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	sidesScrollArea->setWidget(sidesContainer);

	auto* detailsCard = new QFrame(this);
	detailsCard->setObjectName(CARD_STYLE);
	detailsCard->setFixedWidth(ITEM_PANEL_WIDTH);
	noSelectionLabel = CreatePlainLabel(detailsCard, SECONDARY_TEXT_STYLE);
	noSelectionLabel->setAlignment(Qt::AlignCenter);
	itemPanel = new QWidget(detailsCard);
	itemPanel->setObjectName(DETAILS_PAGE_STYLE);
	itemSideLabel = CreatePlainLabel(itemPanel);
	MakeSectionLabel(*itemSideLabel);
	itemPicture = new SteamPicture(imageCache, ITEM_PICTURE_EDGE, itemPanel);
	itemNameLabel = CreatePlainLabel(itemPanel, ROW_NAME_STYLE);
	itemNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	itemTypeLabel = CreatePlainLabel(itemPanel, SECONDARY_TEXT_STYLE);
	itemTagsLabel = CreatePlainLabel(itemPanel);
	itemTagsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	itemWarningsLabel = CreatePlainLabel(itemPanel, ERROR_TEXT_STYLE);
	itemFlagsLabel = CreatePlainLabel(itemPanel, SECONDARY_TEXT_STYLE);
	auto* itemLayout = new QVBoxLayout(itemPanel);
	itemLayout->setContentsMargins(0, 0, 0, 0);
	itemLayout->setSpacing(SECTION_SPACING);
	itemLayout->addWidget(itemSideLabel);
	itemLayout->addWidget(itemPicture, 0, Qt::AlignHCenter);
	itemLayout->addWidget(itemNameLabel);
	itemLayout->addWidget(itemTypeLabel);
	itemLayout->addWidget(itemTagsLabel);
	itemLayout->addWidget(itemWarningsLabel);
	itemLayout->addWidget(itemFlagsLabel);
	itemLayout->addStretch();
	// The description scrolls in a low window and the actions stay in sight under it.
	itemScrollArea = new QScrollArea(detailsCard);
	itemScrollArea->setObjectName(DETAILS_SCROLL_STYLE);
	itemScrollArea->setFrameShape(QFrame::NoFrame);
	itemScrollArea->setWidgetResizable(true);
	itemScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	itemScrollArea->setWidget(itemPanel);
	itemActions = new QWidget(detailsCard);
	viewerButton = new QPushButton(itemActions);
	SetButtonRole(*viewerButton, ButtonRole::PRIMARY);
	inspectButton = new QPushButton(itemActions);
	marketButton = new QPushButton(itemActions);
	inventoryButton = new QPushButton(itemActions);
	auto* actionsLayout = new QVBoxLayout(itemActions);
	actionsLayout->setContentsMargins(0, SECTION_SPACING, 0, 0);
	actionsLayout->setSpacing(SECTION_SPACING);
	actionsLayout->addWidget(viewerButton);
	actionsLayout->addWidget(inspectButton);
	actionsLayout->addWidget(marketButton);
	actionsLayout->addWidget(inventoryButton);
	auto* detailsLayout = new QVBoxLayout(detailsCard);
	detailsLayout->setContentsMargins(PANEL_MARGINS);
	detailsLayout->setSpacing(0);
	detailsLayout->addWidget(noSelectionLabel, 1);
	detailsLayout->addWidget(itemScrollArea, 1);
	detailsLayout->addWidget(itemActions);

	auto* bodyLayout = new QHBoxLayout();
	bodyLayout->setSpacing(DIALOG_SPACING);
	bodyLayout->addWidget(sidesScrollArea, 1);
	bodyLayout->addWidget(detailsCard);

	accountLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	statusLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	rejectButton = new QPushButton(this);
	acceptButton = new QPushButton(this);
	SetButtonRole(*acceptButton, ButtonRole::PRIMARY);
	closeButton = new QPushButton(this);
	auto* footerTextLayout = new QVBoxLayout();
	footerTextLayout->addWidget(accountLabel);
	footerTextLayout->addWidget(statusLabel);
	auto* footerLayout = new QHBoxLayout();
	footerLayout->addLayout(footerTextLayout, 1);
	footerLayout->addWidget(closeButton);
	footerLayout->addWidget(rejectButton);
	footerLayout->addWidget(acceptButton);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addLayout(headerLayout);
	dialogLayout->addWidget(holdLabel);
	dialogLayout->addWidget(warningsLabel);
	dialogLayout->addWidget(messageLabel);
	dialogLayout->addLayout(bodyLayout, 1);
	dialogLayout->addLayout(footerLayout);

	connect(profileButton, &QPushButton::clicked, this, [this]() {
		if (offer.has_value()) {
			OpenLink(SteamProfileUrl(offer->partnerSteamId));
		}
	});
	connect(steamOfferButton, &QPushButton::clicked, this, [this]() {
		if (confirmation.has_value()) {
			OpenLink(TradeOfferUrl(confirmation->creatorId));
		}
	});
	connect(inspectButton, &QPushButton::clicked, this, [this]() {
		if (const TradeItem* item = SelectedItem(); item != nullptr) {
			OpenLink(QUrl(item->inspectLink));
		}
	});
	connect(viewerButton, &QPushButton::clicked, this, [this]() {
		const TradeItem* item = SelectedItem();
		const std::optional<QString> itemCode = item != nullptr ? InspectItemCode(item->inspectLink) : std::nullopt;
		if (itemCode.has_value()) {
			OpenLink(ReskinsViewerUrl(*itemCode, QLocale().language() == QLocale::Russian));
		}
	});
	connect(marketButton, &QPushButton::clicked, this, [this]() {
		if (const TradeItem* item = SelectedItem(); item != nullptr) {
			OpenLink(MarketListingUrl(*item));
		}
	});
	connect(inventoryButton, &QPushButton::clicked, this, [this]() {
		if (const TradeItem* item = SelectedItem(); item != nullptr) {
			OpenLink(InventoryItemUrl(*item));
		}
	});
	connect(acceptButton, &QPushButton::clicked, this, [this]() {
		Respond(ConfirmationDecision::ACCEPT);
	});
	connect(rejectButton, &QPushButton::clicked, this, [this]() {
		Respond(ConfirmationDecision::REJECT);
	});
	connect(closeButton, &QPushButton::clicked, this, &TradeOfferDialog::close);
	connect(&accountConfirmations, &AccountConfirmations::Changed, this, &TradeOfferDialog::UpdateState);
	// The list belongs to the account; when it is replaced, this window has nothing left to decide on.
	connect(&accountConfirmations, &QObject::destroyed, this, &TradeOfferDialog::close);

	RetranslateUi();
}

void TradeOfferDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void TradeOfferDialog::RetranslateUi() {
	profileButton->setText(tr("Partner profile in Steam"));
	steamOfferButton->setText(tr("Open the trade in Steam"));
	noSelectionLabel->setText(tr("Select an item to check it: its exterior, warnings of Steam, a 3D view and its page in Steam."));
	viewerButton->setText(tr("Inspect in 3D on reskins.gg"));
	inspectButton->setText(tr("Inspect in game"));
	marketButton->setText(tr("Steam Community Market"));
	inventoryButton->setText(tr("Item in the Steam inventory"));
	closeButton->setText(tr("Close"));
	rejectButton->setText(tr("Decline"));
	acceptButton->setText(tr("Confirm"));
	accountLabel->setText(tr("The decision is made for the account %1 (%2).").arg(accountTitle, SteamIdText(record)));
	loadingLabel->setText(tr("Loading the items of the trade from Steam…"));
	RetranslateSides();
	UpdateState();
	ShowSelectedItem();
}

void TradeOfferDialog::RetranslateSides() {
	for (const TradeSide side : {TradeSide::GIVE, TradeSide::RECEIVE}) {
		const SideView& view = ViewOf(side);
		const bool isGiven = side == TradeSide::GIVE;
		if (view.titleLabel != nullptr) {
			view.titleLabel->setText(isGiven ? tr("You give: %n item(s)", nullptr, view.itemCount) : tr("You receive: %n item(s)", nullptr, view.itemCount));
		}
		if (view.emptyLabel != nullptr) {
			view.emptyLabel->setText(isGiven ? tr("You give nothing.") : tr("You receive nothing."));
		}
	}
}

void TradeOfferDialog::UpdateState() {
	if (confirmations != nullptr) {
		const std::vector<PendingConfirmation>& entries = confirmations->Confirmations();
		const auto entry = std::ranges::find(entries, confirmationId, &PendingConfirmation::id);
		if (entry != entries.end()) {
			confirmation = *entry;
		}
		if (!offer.has_value() && confirmation.has_value()) {
			if (const TradeOfferDetails* loadedOffer = confirmations->FindTradeOffer(confirmation->creatorId); loadedOffer != nullptr) {
				offer = *loadedOffer;
				ShowItems(*loadedOffer);
			}
		}
	}
	const QString partnerName = confirmation.has_value() ? confirmation->headline : QString();
	setWindowTitle(tr("Trade with %1 · %2").arg(partnerName, accountTitle));
	partnerNameLabel->setText(partnerName);
	partnerPicture->SetUrl(confirmation.has_value() ? confirmation->iconUrl : QUrl());
	QStringList partnerDetails;
	if (offer.has_value()) {
		partnerDetails.append(offer->partnerSteamId != 0 ? tr("SteamID %1").arg(offer->partnerSteamId) : tr("SteamID unknown"));
		partnerDetails.append(offer->isOwnOffer ? tr("you sent this offer") : tr("the partner sent this offer"));
		if (offer->expiresAt.isValid()) {
			partnerDetails.append(tr("valid until %1").arg(FormatTime(offer->expiresAt)));
		}
	}
	partnerDetailsLabel->setText(partnerDetails.join(QStringLiteral(" · ")));
	profileButton->setEnabled(offer.has_value() && offer->partnerSteamId != 0);
	steamOfferButton->setEnabled(confirmation.has_value() && TradeOfferUrl(confirmation->creatorId).isValid());

	const bool isHeld = offer.has_value() && offer->holdEndsAt.isValid() && offer->holdEndsAt > QDateTime::currentDateTimeUtc();
	holdLabel->setText(isHeld ? tr("Steam holds the items of this trade until %1 before they change hands.").arg(FormatTime(offer->holdEndsAt)) : QString());
	holdLabel->setVisible(isHeld);
	const QStringList warnings = confirmation.has_value() ? confirmation->warnings : QStringList();
	warningsLabel->setText(warnings.join(QLatin1Char('\n')));
	warningsLabel->setVisible(!warnings.isEmpty());
	const QString message = offer.has_value() ? offer->message.trimmed() : QString();
	messageLabel->setText(tr("Message: %1").arg(message));
	messageLabel->setVisible(!message.isEmpty());
	loadingLabel->setVisible(!offer.has_value());
	loadingTiles->setVisible(!offer.has_value());

	const bool canDecide = IsListed() && confirmations->Activity() == ConfirmationsActivity::IDLE;
	acceptButton->setEnabled(canDecide);
	rejectButton->setEnabled(canDecide);
	statusLabel->setText(StatusText());
	statusLabel->setVisible(!statusLabel->text().isEmpty());
}

void TradeOfferDialog::ShowItems(const TradeOfferDetails& loadedOffer) {
	sidesLayout->insertWidget(0, CreateSide(TradeSide::GIVE, loadedOffer.itemsToGive));
	sidesLayout->insertWidget(1, CreateSide(TradeSide::RECEIVE, loadedOffer.itemsToReceive));
	RetranslateSides();
	// Steam lists what the account gives first; showing it at once matters most for the account's safety.
	if (!loadedOffer.itemsToGive.empty()) {
		SelectItem(TradeSide::GIVE, 0);
	} else if (!loadedOffer.itemsToReceive.empty()) {
		SelectItem(TradeSide::RECEIVE, 0);
	}
}

QWidget* TradeOfferDialog::CreateSide(TradeSide side, const std::vector<TradeItem>& items) {
	auto* sideCard = new QFrame(sidesContainer);
	sideCard->setObjectName(CARD_STYLE);
	SideView& view = ViewOf(side);
	view.itemCount = static_cast<int>(items.size());
	view.titleLabel = CreatePlainLabel(sideCard);
	MakeSectionLabel(*view.titleLabel);
	auto* sideLayout = new QVBoxLayout(sideCard);
	sideLayout->setContentsMargins(PANEL_MARGINS);
	sideLayout->setSpacing(SECTION_SPACING);
	sideLayout->addWidget(view.titleLabel);
	if (items.empty()) {
		view.emptyLabel = CreatePlainLabel(sideCard, SECONDARY_TEXT_STYLE);
		sideLayout->addWidget(view.emptyLabel);
		return sideCard;
	}
	view.grid = new ItemGrid(imageCache, items, sideCard);
	connect(view.grid, &ItemGrid::ItemClicked, this, [this, side](std::size_t itemIndex) {
		SelectItem(side, itemIndex);
	});
	sideLayout->addWidget(view.grid);
	return sideCard;
}

void TradeOfferDialog::SelectItem(TradeSide side, std::size_t itemIndex) {
	selection = std::make_pair(side, itemIndex);
	for (const TradeSide shownSide : {TradeSide::GIVE, TradeSide::RECEIVE}) {
		if (ItemGrid* grid = ViewOf(shownSide).grid; grid != nullptr) {
			grid->SetSelectedIndex(shownSide == side ? std::optional<std::size_t>(itemIndex) : std::nullopt);
		}
	}
	ShowSelectedItem();
}

void TradeOfferDialog::ShowSelectedItem() {
	const TradeItem* item = SelectedItem();
	noSelectionLabel->setVisible(item == nullptr);
	itemScrollArea->setVisible(item != nullptr);
	itemActions->setVisible(item != nullptr);
	if (item == nullptr) {
		return;
	}
	const bool isGiven = selection.has_value() && selection->first == TradeSide::GIVE;
	itemSideLabel->setText(isGiven ? tr("You give this item") : tr("You receive this item"));
	itemPicture->SetUrl(item->largeImageUrl.isEmpty() ? item->imageUrl : item->largeImageUrl);
	itemNameLabel->setText(ItemDisplayName(*item) + (item->amount > 1 ? QStringLiteral(" ×%1").arg(item->amount) : QString()));
	itemNameLabel->setStyleSheet(item->nameColor.isValid() ? QStringLiteral("color: %1;").arg(item->nameColor.name()) : QString());
	itemTypeLabel->setText(item->type);
	itemTypeLabel->setVisible(!item->type.isEmpty());
	QStringList tagLines;
	for (const ItemTag& tag : item->tags) {
		tagLines.append(tag.category.isEmpty() ? tag.name : tr("%1: %2").arg(tag.category, tag.name));
	}
	itemTagsLabel->setText(tagLines.join(QLatin1Char('\n')));
	itemTagsLabel->setVisible(!tagLines.isEmpty());
	itemWarningsLabel->setText(item->fraudWarnings.join(QLatin1Char('\n')));
	itemWarningsLabel->setVisible(!item->fraudWarnings.isEmpty());
	itemFlagsLabel->setText(tr("Tradable: %1 · Marketable: %2").arg(item->isTradable ? tr("yes") : tr("no"), item->isMarketable ? tr("yes") : tr("no")));

	const bool hasItemCode = InspectItemCode(item->inspectLink).has_value();
	viewerButton->setEnabled(hasItemCode);
	viewerButton->setToolTip(hasItemCode ? QString() : tr("Steam gave no item code for this item, so reskins.gg cannot show it."));
	inspectButton->setEnabled(!item->inspectLink.isEmpty());
	marketButton->setEnabled(MarketListingUrl(*item).isValid());
	inventoryButton->setEnabled(InventoryItemUrl(*item).isValid());
}

void TradeOfferDialog::Respond(ConfirmationDecision decision) {
	if (confirmations != nullptr && confirmations->Respond({confirmationId}, decision)) {
		wasDecisionSent = true;
	}
	UpdateState();
}

TradeOfferDialog::SideView& TradeOfferDialog::ViewOf(TradeSide side) {
	return sideViews.at(std::to_underlying(side));
}

const TradeItem* TradeOfferDialog::SelectedItem() const {
	if (!offer.has_value() || !selection.has_value()) {
		return nullptr;
	}
	const std::vector<TradeItem>& items = selection->first == TradeSide::GIVE ? offer->itemsToGive : offer->itemsToReceive;
	return selection->second < items.size() ? &items.at(selection->second) : nullptr;
}

bool TradeOfferDialog::IsListed() const {
	if (confirmations == nullptr) {
		return false;
	}
	return std::ranges::find(confirmations->Confirmations(), confirmationId, &PendingConfirmation::id) != confirmations->Confirmations().end();
}

QString TradeOfferDialog::StatusText() const {
	if (confirmations != nullptr && confirmations->IsResponding(confirmationId)) {
		return tr("Sending the decision to Steam…");
	}
	if (!IsListed()) {
		if (wasDecisionSent && confirmations != nullptr && !confirmations->ResponseFailure().has_value()) {
			return tr("The decision was sent; this confirmation no longer waits.");
		}
		return tr("This confirmation no longer waits: it was decided elsewhere or it expired.");
	}
	if (wasDecisionSent && confirmations->ResponseFailure().has_value()) {
		return tr("Steam did not confirm the decision. The list was loaded again: this trade still waits.");
	}
	return {};
}

} // namespace SteamForge
