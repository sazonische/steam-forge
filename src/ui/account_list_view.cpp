#include "ui/account_list_view.h"

#include "steam/guard_code.h"
#include "steam/steam_clock.h"
#include "ui/account_row.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr std::chrono::milliseconds CODE_REFRESH_INTERVAL{200};
	constexpr std::chrono::milliseconds CODE_PERIOD{GUARD_CODE_PERIOD};
	constexpr std::chrono::milliseconds EXPIRY_WARNING_TIME{std::chrono::seconds(5)};
	constexpr int COUNTDOWN_RESOLUTION = 1000;
	constexpr QMargins HEADER_MARGINS{16, 12, 16, 10};
	constexpr QMargins ROWS_MARGINS{6, 6, 6, 6};
	constexpr int HEADER_SPACING = 6;
	constexpr int ROW_SPACING = 2;
} // namespace

AccountListView::AccountListView(const SteamClock& clock, AccountProfiles& profiles, QWidget* parent) :
	QWidget(parent),
	steamClock(clock),
	accountProfiles(profiles) {
	setObjectName(ACCOUNT_LIST_STYLE);
	setAttribute(Qt::WA_StyledBackground, true);

	countdownLabel = new QLabel(this);
	countdownLabel->setObjectName(SECONDARY_TEXT_STYLE);
	countdownBar = new QProgressBar(this);
	countdownBar->setObjectName(CODE_COUNTDOWN_STYLE);
	countdownBar->setRange(0, COUNTDOWN_RESOLUTION);
	countdownBar->setTextVisible(false);

	auto* rowsContainer = new QWidget(this);
	rowsContainer->setObjectName(ACCOUNT_ROWS_STYLE);
	rowsLayout = new QVBoxLayout(rowsContainer);
	rowsLayout->setContentsMargins(ROWS_MARGINS);
	rowsLayout->setSpacing(ROW_SPACING);
	rowsLayout->addStretch();

	scrollArea = new QScrollArea(this);
	scrollArea->setObjectName(ACCOUNT_SCROLL_STYLE);
	scrollArea->setFrameShape(QFrame::NoFrame);
	scrollArea->setWidgetResizable(true);
	scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	scrollArea->setWidget(rowsContainer);

	auto* headerLayout = new QVBoxLayout();
	headerLayout->setContentsMargins(HEADER_MARGINS);
	headerLayout->setSpacing(HEADER_SPACING);
	headerLayout->addWidget(countdownLabel);
	headerLayout->addWidget(countdownBar);
	auto* viewLayout = new QVBoxLayout(this);
	viewLayout->setContentsMargins(0, 0, 0, 0);
	viewLayout->setSpacing(0);
	viewLayout->addLayout(headerLayout);
	viewLayout->addWidget(scrollArea, 1);

	codeTimer.setInterval(CODE_REFRESH_INTERVAL);
	connect(&codeTimer, &QTimer::timeout, this, &AccountListView::UpdateCodes);
	connect(&clock, &SteamClock::StateChanged, this, &AccountListView::UpdateCodes);
}

void AccountListView::SetRecords(std::span<const AuthenticatorRecord> records) {
	for (AccountRow* row : rows) {
		row->deleteLater();
	}
	rows.clear();

	for (const AuthenticatorRecord& record : records) {
		auto* row = new AccountRow(record, accountProfiles, scrollArea->widget());
		rowsLayout->insertWidget(rowsLayout->count() - 1, row);
		connect(row, &AccountRow::SelectionRequested, this, &AccountListView::SelectRecord);
		connect(row, &AccountRow::NeighbourRequested, this, [this, recordId = record.id](int step) {
			SelectNeighbour(recordId, step);
		});
		connect(row, &AccountRow::RevealToggled, this, [this](const QUuid& recordId, bool isShown) {
			if (isShown) {
				revealedRecordIds.insert(recordId);
			} else {
				revealedRecordIds.remove(recordId);
			}
			Q_EMIT RevealChanged(recordId, isShown);
		});
		row->SetRevealed(revealedRecordIds.contains(record.id));
		row->SetSelected(selectedRecordId == record.id);
		rows.push_back(row);
	}
	if (selectedRecordId.has_value() && FindRow(*selectedRecordId) == nullptr) {
		selectedRecordId.reset();
	}
	UpdateCodes();
}

void AccountListView::SelectRecord(const QUuid& recordId) {
	AccountRow* selectedRow = FindRow(recordId);
	if (selectedRow == nullptr) {
		return;
	}
	for (AccountRow* row : rows) {
		row->SetSelected(row == selectedRow);
	}
	scrollArea->ensureWidgetVisible(selectedRow);
	if (selectedRecordId == recordId) {
		return;
	}
	selectedRecordId = recordId;
	Q_EMIT RecordSelected(recordId);
}

std::optional<QUuid> AccountListView::SelectedRecordId() const {
	return selectedRecordId;
}

bool AccountListView::Contains(const QUuid& recordId) const {
	return FindRow(recordId) != nullptr;
}

bool AccountListView::IsRevealed(const QUuid& recordId) const {
	return revealedRecordIds.contains(recordId);
}

void AccountListView::SetConfirmationCount(const QUuid& recordId, int waitingCount) {
	if (AccountRow* row = FindRow(recordId); row != nullptr) {
		row->SetConfirmationCount(waitingCount);
	}
}

void AccountListView::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		UpdateCodes();
	}
	QWidget::changeEvent(event);
}

void AccountListView::showEvent(QShowEvent* event) {
	QWidget::showEvent(event);
	UpdateCodes();
	codeTimer.start();
}

void AccountListView::hideEvent(QHideEvent* event) {
	codeTimer.stop();
	QWidget::hideEvent(event);
}

void AccountListView::UpdateCodes() {
	const std::chrono::sys_time<std::chrono::milliseconds> steamTime = steamClock.Now();
	const auto steamSeconds = std::chrono::floor<std::chrono::seconds>(steamTime);
	for (AccountRow* row : rows) {
		row->UpdateCode(steamSeconds);
	}

	const std::chrono::milliseconds remainingTime = CODE_PERIOD - (steamTime.time_since_epoch() % CODE_PERIOD);
	countdownBar->setValue(static_cast<int>(remainingTime * COUNTDOWN_RESOLUTION / CODE_PERIOD));
	SetStyleFlag(*countdownBar, StyleFlag::EXPIRING, remainingTime < EXPIRY_WARNING_TIME);
	const auto remainingSeconds = static_cast<int>(std::chrono::ceil<std::chrono::seconds>(remainingTime).count());
	countdownLabel->setText(tr("Codes change in %n s", nullptr, remainingSeconds));
}

void AccountListView::SelectNeighbour(const QUuid& recordId, int step) {
	const auto rowIterator = std::ranges::find(rows, recordId, &AccountRow::RecordId);
	if (rowIterator == rows.end()) {
		return;
	}
	const auto currentIndex = static_cast<int>(std::distance(rows.begin(), rowIterator));
	const int targetIndex = std::clamp(currentIndex + step, 0, static_cast<int>(rows.size()) - 1);
	AccountRow* targetRow = rows.at(static_cast<std::size_t>(targetIndex));
	SelectRecord(targetRow->RecordId());
	targetRow->setFocus(Qt::OtherFocusReason);
}

AccountRow* AccountListView::FindRow(const QUuid& recordId) const {
	const auto rowIterator = std::ranges::find(rows, recordId, &AccountRow::RecordId);
	if (rowIterator == rows.end()) {
		return nullptr;
	}
	return *rowIterator;
}

} // namespace SteamForge
