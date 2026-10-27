#pragma once

#include "domain/authenticator_record.h"
#include "ui/account_row.h"

namespace SteamForge {

class AccountProfiles;
class AccountRow;
class SteamClock;

// All accounts with their live codes; Steam codes of every account change at the same moment.
class AccountListView final : public QWidget {
	Q_OBJECT

public:
	AccountListView(const SteamClock& clock, AccountProfiles& profiles, QWidget* parent = nullptr);

	void SetRecords(std::span<const AuthenticatorRecord> records);
	void SelectRecord(const QUuid& recordId);
	[[nodiscard]] std::optional<QUuid> SelectedRecordId() const;
	[[nodiscard]] bool Contains(const QUuid& recordId) const;
	void SetConfirmationCount(const QUuid& recordId, int waitingCount);
	void SetCloudState(const QUuid& recordId, RowCloudState state);
	[[nodiscard]] bool IsRevealed(const QUuid& recordId) const;

Q_SIGNALS:
	void RecordSelected(const QUuid& recordId);
	void RevealChanged(const QUuid& recordId, bool isRevealed);

protected:
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;
	void hideEvent(QHideEvent* event) override;

private:
	void UpdateCodes();
	void SelectNeighbour(const QUuid& recordId, int step);
	[[nodiscard]] AccountRow* FindRow(const QUuid& recordId) const;

	const SteamClock& steamClock;
	AccountProfiles& accountProfiles;
	std::vector<AccountRow*> rows;
	std::optional<QUuid> selectedRecordId;
	// Kept here, so rebuilding the rows does not show what the user hid or hide what they opened.
	QSet<QUuid> revealedRecordIds;
	QTimer codeTimer;

	QLabel* countdownLabel = nullptr;
	QProgressBar* countdownBar = nullptr;
	QScrollArea* scrollArea = nullptr;
	QVBoxLayout* rowsLayout = nullptr;
};

} // namespace SteamForge
