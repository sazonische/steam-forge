#include "ui/confirmations_dialog.h"

#include "app/confirmation_monitor.h"
#include "ui/confirmation_card.h"
#include "ui/illustration_view.h"
#include "ui/record_presentation.h"
#include "ui/steam_picture.h"
#include "ui/trade_offer_dialog.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr QSize INITIAL_DIALOG_SIZE{780, 760};
	constexpr QSize MINIMUM_DIALOG_SIZE{640, 480};
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr int DIALOG_SPACING = 12;
	constexpr int AVATAR_EDGE = 56;
	constexpr int HEADER_TEXT_SPACING = 2;
	constexpr int ENTRY_SPACING = 8;
	constexpr int FILTER_SPACING = 4;
	constexpr int EMPTY_STATE_TOP_MARGIN = 24;
	constexpr int EMPTY_STATE_SPACING = 8;
	constexpr int EMPTY_STATE_BUTTON_SPACING = 8;
	constexpr QLatin1StringView EMPTY_ILLUSTRATION_RESOURCE{":/illustrations/no_confirmations.svg"};
	constexpr QLatin1StringView SIGN_IN_ILLUSTRATION_RESOURCE{":/illustrations/sign_in_required.svg"};
	constexpr QSize EMPTY_ILLUSTRATION_SIZE{360, 200};
	// Cards, the empty-list state and the closing stretch: the cards always come first.
	constexpr int TRAILING_LAYOUT_ITEMS = 2;
	constexpr std::size_t ALL_FILTER_INDEX = 0;
	constexpr std::size_t TRADES_FILTER_INDEX = 1;
	constexpr std::size_t MARKET_FILTER_INDEX = 2;
	constexpr std::size_t OTHER_FILTER_INDEX = 3;

	QLabel* CreatePlainLabel(QWidget* parent, QLatin1StringView styleName = {}) {
		auto* label = new QLabel(parent);
		label->setTextFormat(Qt::PlainText);
		label->setWordWrap(true);
		if (!styleName.isEmpty()) {
			label->setObjectName(styleName);
		}
		return label;
	}

	bool NeedsSignIn(const AccountConfirmations& confirmations) {
		const std::optional<ConfirmationsFailure>& failure = confirmations.LoadFailure();
		return failure.has_value() && failure->kind == ConfirmationsFailureKind::SIGN_IN_REQUIRED;
	}

	int CountInCategory(const std::vector<PendingConfirmation>& entries, ConfirmationCategory category) {
		return static_cast<int>(std::ranges::count(entries, category, &PendingConfirmation::category));
	}
} // namespace

ConfirmationsDialog::ConfirmationsDialog(ConfirmationMonitor& monitor, SteamImageCache& images, AuthenticatorRecord accountRecord, QString title, const QImage& accountAvatar, QWidget* parent) :
	QDialog(parent),
	confirmationMonitor(monitor),
	imageCache(images),
	record(std::move(accountRecord)),
	accountTitle(std::move(title)) {
	resize(INITIAL_DIALOG_SIZE);
	setMinimumSize(MINIMUM_DIALOG_SIZE);

	// The account the decisions are made for, the way the account list shows it.
	avatarPicture = new PictureView(AVATAR_EDGE, this);
	avatarPicture->SetPicture(accountAvatar);
	avatarPicture->setVisible(!accountAvatar.isNull());
	accountNameLabel = CreatePlainLabel(this, PAGE_TITLE_STYLE);
	accountDetailsLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	statusLabel = CreatePlainLabel(this, SECONDARY_TEXT_STYLE);
	responseFailureLabel = CreatePlainLabel(this, NOTICE_STYLE);

	filterBar = new QWidget(this);
	auto* filterLayout = new QHBoxLayout(filterBar);
	filterLayout->setContentsMargins(0, 0, 0, 0);
	filterLayout->setSpacing(FILTER_SPACING);
	for (std::size_t i = 0; i < filterButtons.size(); ++i) {
		auto* filterButton = new QPushButton(filterBar);
		filterButton->setObjectName(FILTER_TAB_STYLE);
		filterButton->setCheckable(true);
		filterButton->setCursor(Qt::PointingHandCursor);
		filterGroup.addButton(filterButton, static_cast<int>(i));
		filterLayout->addWidget(filterButton);
		filterButtons.at(i) = filterButton;
	}
	filterLayout->addStretch();
	filterButtons.at(ALL_FILTER_INDEX)->setChecked(true);

	selectAllButton = new QPushButton(this);
	acceptSelectedButton = new QPushButton(this);
	SetButtonRole(*acceptSelectedButton, ButtonRole::PRIMARY);
	rejectSelectedButton = new QPushButton(this);
	auto* selectionLayout = new QHBoxLayout();
	selectionLayout->addWidget(selectAllButton);
	selectionLayout->addStretch();
	selectionLayout->addWidget(rejectSelectedButton);
	selectionLayout->addWidget(acceptSelectedButton);

	entriesContainer = new QWidget(this);
	entriesContainer->setObjectName(DETAILS_PAGE_STYLE);
	entriesLayout = new QVBoxLayout(entriesContainer);
	entriesLayout->setContentsMargins(0, 0, 0, 0);
	entriesLayout->setSpacing(ENTRY_SPACING);
	emptyState = new QWidget(entriesContainer);
	emptyIllustration = new IllustrationView(EMPTY_ILLUSTRATION_RESOURCE, EMPTY_ILLUSTRATION_SIZE, emptyState);
	emptyTitleLabel = CreatePlainLabel(emptyState, PAGE_TITLE_STYLE);
	emptyTitleLabel->setAlignment(Qt::AlignCenter);
	emptyLabel = CreatePlainLabel(emptyState, EMPTY_STATE_TEXT_STYLE);
	emptyLabel->setAlignment(Qt::AlignCenter);
	signInButton = new QPushButton(emptyState);
	SetButtonRole(*signInButton, ButtonRole::PRIMARY);
	auto* emptyStateLayout = new QVBoxLayout(emptyState);
	emptyStateLayout->setContentsMargins(0, EMPTY_STATE_TOP_MARGIN, 0, 0);
	emptyStateLayout->setSpacing(EMPTY_STATE_SPACING);
	emptyStateLayout->addWidget(emptyIllustration, 0, Qt::AlignHCenter);
	emptyStateLayout->addWidget(emptyTitleLabel);
	emptyStateLayout->addWidget(emptyLabel);
	emptyStateLayout->addSpacing(EMPTY_STATE_BUTTON_SPACING);
	emptyStateLayout->addWidget(signInButton, 0, Qt::AlignHCenter);
	entriesLayout->addWidget(emptyState);
	entriesLayout->addStretch();
	auto* entriesScrollArea = new QScrollArea(this);
	entriesScrollArea->setObjectName(DETAILS_SCROLL_STYLE);
	entriesScrollArea->setFrameShape(QFrame::NoFrame);
	entriesScrollArea->setWidgetResizable(true);
	entriesScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	entriesScrollArea->setWidget(entriesContainer);

	refreshButton = new QPushButton(this);
	closeButton = new QPushButton(this);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(refreshButton);
	buttonsLayout->addWidget(closeButton);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	auto* headerTextLayout = new QVBoxLayout();
	headerTextLayout->setSpacing(HEADER_TEXT_SPACING);
	headerTextLayout->addStretch();
	headerTextLayout->addWidget(accountNameLabel);
	headerTextLayout->addWidget(accountDetailsLabel);
	headerTextLayout->addWidget(statusLabel);
	headerTextLayout->addStretch();
	auto* headerLayout = new QHBoxLayout();
	headerLayout->setSpacing(DIALOG_SPACING);
	headerLayout->addWidget(avatarPicture, 0, Qt::AlignVCenter);
	headerLayout->addLayout(headerTextLayout, 1);
	dialogLayout->addLayout(headerLayout);
	dialogLayout->addWidget(responseFailureLabel);
	dialogLayout->addWidget(filterBar);
	dialogLayout->addLayout(selectionLayout);
	dialogLayout->addWidget(entriesScrollArea, 1);
	dialogLayout->addLayout(buttonsLayout);

	connect(&filterGroup, &QButtonGroup::idClicked, this, [this](int filterIndex) {
		SelectFilter(static_cast<EntryFilter>(filterIndex));
	});
	connect(selectAllButton, &QPushButton::clicked, this, &ConfirmationsDialog::ToggleSelectAll);
	connect(acceptSelectedButton, &QPushButton::clicked, this, [this]() {
		RespondToSelected(ConfirmationDecision::ACCEPT);
	});
	connect(rejectSelectedButton, &QPushButton::clicked, this, [this]() {
		RespondToSelected(ConfirmationDecision::REJECT);
	});
	connect(refreshButton, &QPushButton::clicked, this, [this]() {
		if (confirmations != nullptr) {
			confirmations->Refresh();
		}
	});
	connect(signInButton, &QPushButton::clicked, this, [this]() {
		Q_EMIT SignInRequested(record.id);
	});
	connect(closeButton, &QPushButton::clicked, this, &ConfirmationsDialog::close);
	connect(&confirmationMonitor, &ConfirmationMonitor::AccountsChanged, this, &ConfirmationsDialog::Bind);

	Bind();
	RetranslateUi();
}

const QUuid& ConfirmationsDialog::RecordId() const {
	return record.id;
}

void ConfirmationsDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void ConfirmationsDialog::showEvent(QShowEvent* event) {
	QDialog::showEvent(event);
	// A rejected sign-in stays rejected until the next sign-in, which the monitor notices by itself.
	if (!hasRefreshedOnOpen && confirmations != nullptr && !NeedsSignIn(*confirmations)) {
		hasRefreshedOnOpen = true;
		confirmations->Refresh();
	}
}

void ConfirmationsDialog::Bind() {
	AccountConfirmations* currentConfirmations = confirmationMonitor.Find(record.id);
	if (currentConfirmations == confirmations) {
		return;
	}
	if (confirmations != nullptr) {
		disconnect(confirmations, nullptr, this, nullptr);
	}
	CloseTradeOffers();
	confirmations = currentConfirmations;
	selectedIds.clear();
	if (confirmations != nullptr) {
		connect(confirmations, &AccountConfirmations::Changed, this, &ConfirmationsDialog::UpdateState);
		if (isVisible()) {
			confirmations->Refresh();
		}
	}
	UpdateState();
}

void ConfirmationsDialog::RetranslateUi() {
	const QString& accountName = accountTitle;
	setWindowTitle(tr("Confirmations · %1").arg(accountName));
	accountNameLabel->setText(accountName);
	accountDetailsLabel->setText(tr("Trade and market confirmations of this account."));
	accountDetailsLabel->setToolTip(SteamIdText(record));
	refreshButton->setText(tr("Refresh"));
	signInButton->setText(tr("Sign in to Steam…"));
	closeButton->setText(tr("Close"));
	UpdateState();
}

void ConfirmationsDialog::UpdateState() {
	const bool canDecide = CanDecide();
	const bool isSignInRequired = confirmations == nullptr || NeedsSignIn(*confirmations);
	statusLabel->setText(StatusText());
	// The empty state explains a needed sign-in itself, with its button.
	statusLabel->setVisible(!isSignInRequired);
	const bool hasLoadFailure = confirmations != nullptr && confirmations->LoadFailure().has_value();
	const QLatin1StringView statusStyle = hasLoadFailure ? ERROR_TEXT_STYLE : SECONDARY_TEXT_STYLE;
	if (statusLabel->objectName() != statusStyle) {
		statusLabel->setObjectName(statusStyle);
		statusLabel->style()->unpolish(statusLabel);
		statusLabel->style()->polish(statusLabel);
	}
	const QString responseFailureText = ResponseFailureText();
	responseFailureLabel->setText(responseFailureText);
	responseFailureLabel->setVisible(!responseFailureText.isEmpty());
	refreshButton->setEnabled(confirmations != nullptr && confirmations->Activity() == ConfirmationsActivity::IDLE && !isSignInRequired);
	UpdateFilters();
	SynchronizeCards(canDecide);
	UpdateEmptyState(isSignInRequired);
	UpdateSelectionButtons();
}

void ConfirmationsDialog::UpdateEmptyState(bool isSignInRequired) {
	const bool isLoadedEmpty = confirmations != nullptr && confirmations->HasLoaded() && confirmations->Confirmations().empty();
	// Without the confirmation key a sign-in does not help, so it is not offered.
	const bool canSignInHelp = ConfirmationKeysOf(record).has_value();
	emptyState->setVisible(isSignInRequired || isLoadedEmpty);
	signInButton->setVisible(isSignInRequired && canSignInHelp);
	if (isSignInRequired) {
		emptyIllustration->SetResource(SIGN_IN_ILLUSTRATION_RESOURCE);
		emptyTitleLabel->setText(canSignInHelp ? tr("Sign in to Steam again") : tr("Confirmations are unavailable"));
		emptyLabel->setText(StatusText());
		return;
	}
	emptyIllustration->SetResource(EMPTY_ILLUSTRATION_RESOURCE);
	emptyTitleLabel->setText(tr("Nothing to confirm"));
	emptyLabel->setText(tr("New confirmations will appear here automatically."));
}

void ConfirmationsDialog::SynchronizeCards(bool canDecide) {
	const std::vector<PendingConfirmation> noEntries;
	const std::vector<PendingConfirmation>& entries = confirmations != nullptr ? confirmations->Confirmations() : noEntries;
	QSet<QString> listedIds;
	int position = 0;
	for (const PendingConfirmation& entry : entries) {
		listedIds.insert(entry.id);
		ConfirmationCard* card = cards.value(entry.id);
		if (card == nullptr) {
			card = new ConfirmationCard(imageCache, entriesContainer);
			connect(card, &ConfirmationCard::DecisionRequested, this, &ConfirmationsDialog::Respond);
			connect(card, &ConfirmationCard::SelectionToggled, this, [this](const QString& confirmationId, bool isSelected) {
				if (isSelected) {
					selectedIds.insert(confirmationId);
				} else {
					selectedIds.remove(confirmationId);
				}
				UpdateState();
			});
			connect(card, &ConfirmationCard::TradeDetailsRequested, this, &ConfirmationsDialog::OpenTradeOffer);
			cards.insert(entry.id, card);
		}
		// Cards are kept and only moved, so a background update neither resets the pictures nor the scroll position.
		if (entriesLayout->indexOf(card) != position) {
			entriesLayout->removeWidget(card);
			entriesLayout->insertWidget(position, card);
		}
		const TradeOfferDetails* tradeOffer = entry.category == ConfirmationCategory::TRADE ? confirmations->FindTradeOffer(entry.creatorId) : nullptr;
		const ConfirmationCardState cardState{
			.canDecide = canDecide,
			.isResponding = confirmations->IsResponding(entry.id),
			.isSelected = selectedIds.contains(entry.id),
		};
		card->Show(entry, tradeOffer, cardState);
		card->setVisible(IsShown(entry));
		++position;
	}
	for (auto cardIterator = cards.begin(); cardIterator != cards.end();) {
		if (listedIds.contains(cardIterator.key())) {
			++cardIterator;
			continue;
		}
		// Deleted later: the card may be the one whose button started this update.
		entriesLayout->removeWidget(cardIterator.value());
		cardIterator.value()->hide();
		cardIterator.value()->deleteLater();
		cardIterator = cards.erase(cardIterator);
	}
	Q_ASSERT(entriesLayout->count() == position + TRAILING_LAYOUT_ITEMS);
	selectedIds.intersect(listedIds);
}

void ConfirmationsDialog::UpdateFilters() {
	const std::vector<PendingConfirmation> noEntries;
	const std::vector<PendingConfirmation>& entries = confirmations != nullptr ? confirmations->Confirmations() : noEntries;
	const int tradeCount = CountInCategory(entries, ConfirmationCategory::TRADE);
	const int marketCount = CountInCategory(entries, ConfirmationCategory::MARKET_LISTING);
	const int otherCount = CountInCategory(entries, ConfirmationCategory::OTHER);
	filterButtons.at(ALL_FILTER_INDEX)->setText(tr("All %1").arg(entries.size()));
	filterButtons.at(TRADES_FILTER_INDEX)->setText(tr("Trades %1").arg(tradeCount));
	filterButtons.at(MARKET_FILTER_INDEX)->setText(tr("Market %1").arg(marketCount));
	filterButtons.at(OTHER_FILTER_INDEX)->setText(tr("Other %1").arg(otherCount));
	filterButtons.at(TRADES_FILTER_INDEX)->setVisible(tradeCount > 0);
	filterButtons.at(MARKET_FILTER_INDEX)->setVisible(marketCount > 0);
	filterButtons.at(OTHER_FILTER_INDEX)->setVisible(otherCount > 0);
	const int nonEmptyKinds = static_cast<int>(tradeCount > 0) + static_cast<int>(marketCount > 0) + static_cast<int>(otherCount > 0);
	// One kind of confirmation needs no tabs.
	filterBar->setVisible(nonEmptyKinds > 1);
	const bool isFilterEmpty = (filter == EntryFilter::TRADES && tradeCount == 0) || (filter == EntryFilter::MARKET_LISTINGS && marketCount == 0) || (filter == EntryFilter::OTHER && otherCount == 0);
	if (isFilterEmpty || (filter != EntryFilter::ALL && nonEmptyKinds <= 1)) {
		filter = EntryFilter::ALL;
		filterButtons.at(ALL_FILTER_INDEX)->setChecked(true);
	}
}

void ConfirmationsDialog::SelectFilter(EntryFilter newFilter) {
	filter = newFilter;
	filterButtons.at(static_cast<std::size_t>(filter))->setChecked(true);
	// A selection hidden by the filter would be decided on without being seen.
	selectedIds.clear();
	UpdateState();
}

void ConfirmationsDialog::UpdateSelectionButtons() {
	const QStringList selectableIds = SelectableIds();
	const auto selectedCount = static_cast<int>(SelectedIds().size());
	const bool canDecide = CanDecide();
	const bool areAllSelected = !selectableIds.isEmpty() && selectedCount == selectableIds.size();
	selectAllButton->setText(areAllSelected ? tr("Clear selection") : tr("Select all"));
	selectAllButton->setEnabled(canDecide);
	acceptSelectedButton->setText(tr("Confirm selected (%1)").arg(selectedCount));
	rejectSelectedButton->setText(tr("Decline selected (%1)").arg(selectedCount));
	acceptSelectedButton->setEnabled(canDecide && selectedCount > 0);
	rejectSelectedButton->setEnabled(canDecide && selectedCount > 0);
	for (QPushButton* button : {selectAllButton, acceptSelectedButton, rejectSelectedButton}) {
		button->setVisible(!selectableIds.isEmpty());
	}
}

void ConfirmationsDialog::ToggleSelectAll() {
	const QStringList selectableIds = SelectableIds();
	if (SelectedIds().size() == selectableIds.size()) {
		selectedIds.clear();
	} else {
		selectedIds = QSet<QString>(selectableIds.begin(), selectableIds.end());
	}
	UpdateState();
}

void ConfirmationsDialog::Respond(const QString& confirmationId, ConfirmationDecision decision) {
	if (confirmations != nullptr) {
		std::ignore = confirmations->Respond({confirmationId}, decision);
	}
}

void ConfirmationsDialog::RespondToSelected(ConfirmationDecision decision) {
	const QStringList confirmationIds = SelectedIds();
	if (confirmations == nullptr || confirmationIds.isEmpty()) {
		return;
	}
	const bool isAccepted = decision == ConfirmationDecision::ACCEPT;
	const auto count = static_cast<int>(confirmationIds.size());
	if (count > 1) {
		const QString question = isAccepted ? tr("Confirm %n selected confirmations of %1?", nullptr, count) : tr("Decline %n selected confirmations of %1?", nullptr, count);
		QMessageBox prompt(QMessageBox::Question, windowTitle(), question.arg(accountTitle), QMessageBox::NoButton, this);
		const QPushButton* proceedButton = prompt.addButton(isAccepted ? tr("Confirm") : tr("Decline"), QMessageBox::AcceptRole);
		prompt.setDefaultButton(prompt.addButton(QMessageBox::Cancel));
		prompt.exec();
		if (prompt.clickedButton() != proceedButton || confirmations == nullptr) {
			return;
		}
	}
	if (confirmations->Respond(confirmationIds, decision)) {
		for (const QString& confirmationId : confirmationIds) {
			selectedIds.remove(confirmationId);
		}
	}
	UpdateState();
}

void ConfirmationsDialog::OpenTradeOffer(const QString& confirmationId) {
	if (confirmations == nullptr) {
		return;
	}
	QPointer<TradeOfferDialog> tradeOfferDialog = tradeOfferDialogs.value(confirmationId);
	if (tradeOfferDialog == nullptr) {
		tradeOfferDialog = new TradeOfferDialog(*confirmations, imageCache, record, accountTitle, confirmationId, this);
		tradeOfferDialog->setAttribute(Qt::WA_DeleteOnClose);
		tradeOfferDialogs.insert(confirmationId, tradeOfferDialog);
	}
	tradeOfferDialog->show();
	tradeOfferDialog->raise();
	tradeOfferDialog->activateWindow();
}

void ConfirmationsDialog::CloseTradeOffers() {
	for (const QPointer<TradeOfferDialog>& tradeOfferDialog : std::as_const(tradeOfferDialogs)) {
		if (tradeOfferDialog != nullptr) {
			tradeOfferDialog->close();
		}
	}
	tradeOfferDialogs.clear();
}

bool ConfirmationsDialog::CanDecide() const {
	return confirmations != nullptr && confirmations->Activity() == ConfirmationsActivity::IDLE && confirmations->HasLoaded() && !NeedsSignIn(*confirmations);
}

bool ConfirmationsDialog::IsShown(const PendingConfirmation& confirmation) const {
	switch (filter) {
		case EntryFilter::ALL:
			return true;
		case EntryFilter::TRADES:
			return confirmation.category == ConfirmationCategory::TRADE;
		case EntryFilter::MARKET_LISTINGS:
			return confirmation.category == ConfirmationCategory::MARKET_LISTING;
		case EntryFilter::OTHER:
			return confirmation.category == ConfirmationCategory::OTHER;
	}
	return true;
}

QStringList ConfirmationsDialog::SelectableIds() const {
	QStringList selectableIds;
	if (confirmations == nullptr) {
		return selectableIds;
	}
	for (const PendingConfirmation& confirmation : confirmations->Confirmations()) {
		if (confirmation.allowsBatchResponse && IsShown(confirmation)) {
			selectableIds.append(confirmation.id);
		}
	}
	return selectableIds;
}

QStringList ConfirmationsDialog::SelectedIds() const {
	QStringList orderedIds;
	for (const QString& confirmationId : SelectableIds()) {
		if (selectedIds.contains(confirmationId)) {
			orderedIds.append(confirmationId);
		}
	}
	return orderedIds;
}

QString ConfirmationsDialog::StatusText() const {
	if (confirmations == nullptr) {
		if (!ConfirmationKeysOf(record).has_value()) {
			return tr("The authenticator of %1 has no confirmation key, so confirmations are not available. Add the key in the account panel: the gear button, then \"Add data…\".").arg(accountTitle);
		}
		return tr("There is no valid Steam sign-in for %1. Sign in to Steam to see and confirm trades.").arg(accountTitle);
	}
	if (const std::optional<ConfirmationsFailure>& failure = confirmations->LoadFailure(); failure.has_value()) {
		switch (failure->kind) {
			case ConfirmationsFailureKind::SIGN_IN_REQUIRED:
				return tr("Steam no longer accepts the saved sign-in of %1. Sign in to Steam again to see and confirm trades.").arg(accountTitle);
			case ConfirmationsFailureKind::CONNECTION:
				return DescribeWebApiError(tr("Confirmations could not be loaded."), failure->error, tr("We'll try again automatically."));
			case ConfirmationsFailureKind::STEAM_ERROR:
				return DescribeWebApiError(tr("Confirmations could not be loaded."), failure->error);
			case ConfirmationsFailureKind::STORAGE_ERROR:
				return tr("The renewed Steam sign-in could not be saved on this computer, so nothing was requested.");
		}
	}
	if (!confirmations->HasLoaded()) {
		return tr("Loading confirmations…");
	}
	if (confirmations->Activity() == ConfirmationsActivity::LOADING) {
		return tr("Updating…");
	}
	const QString loadedAt = QLocale().toString(confirmations->LoadedAt().toLocalTime().time(), QLocale::ShortFormat);
	const auto waitingCount = static_cast<int>(confirmations->Confirmations().size());
	if (waitingCount == 0) {
		return tr("Updated at %1.").arg(loadedAt);
	}
	return tr("%n waiting, updated at %1.", nullptr, waitingCount).arg(loadedAt);
}

QString ConfirmationsDialog::ResponseFailureText() const {
	if (confirmations == nullptr || !confirmations->ResponseFailure().has_value()) {
		return {};
	}
	const ConfirmationsFailure& failure = *confirmations->ResponseFailure();
	switch (failure.kind) {
		case ConfirmationsFailureKind::CONNECTION:
			return DescribeWebApiError(tr("It is unknown whether Steam carried out the decision."), failure.error, tr("The list was loaded again: an entry that is still listed was not processed."));
		case ConfirmationsFailureKind::STEAM_ERROR:
			return DescribeWebApiError(tr("Steam did not carry out the decision."), failure.error, tr("The list was loaded again."));
		case ConfirmationsFailureKind::STORAGE_ERROR:
			return tr("The renewed Steam sign-in could not be saved on this computer, so the decision was not sent.");
		case ConfirmationsFailureKind::SIGN_IN_REQUIRED:
			return {};
	}
	return {};
}

} // namespace SteamForge
