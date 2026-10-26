#pragma once

#include "app/account_confirmations.h"

namespace SteamForge {

// Holds the confirmations of every account that is signed in and can sign them, and checks them in the background.
class ConfirmationMonitor final : public QObject {
	Q_OBJECT

public:
	ConfirmationMonitor(WebApiTransport& webApiTransport, CommunityTransport& communityTransport, AccountLibrary& library, const SteamClock& clock, QObject* parent = nullptr);

	void Start();
	// The pointer stays valid until the next AccountsChanged.
	[[nodiscard]] AccountConfirmations* Find(const QUuid& recordId) const;
	// Texts Steam wrote in the previous interface language are loaded again in the new one.
	void RecheckMarketRestrictions();

Q_SIGNALS:
	void AccountsChanged();
	void ConfirmationsArrived(const QUuid& recordId, const std::vector<PendingConfirmation>& newConfirmations);
	void SignInRequired(const QUuid& recordId);

private:
	struct MonitoredAccount {
		AccountConfirmations* confirmations = nullptr;
		// A new sign-in replaces it and lifts a failure that asked for one.
		QString refreshToken;
		std::chrono::steady_clock::time_point nextCheckAt;
		std::chrono::seconds retryDelay{0};
	};

	void SynchronizeAccounts();
	void AddAccount(const QUuid& recordId, const ConfirmationKeys& keys, const QString& refreshToken, std::chrono::steady_clock::time_point firstCheckAt);
	void RemoveAccount(std::map<QUuid, MonitoredAccount>::iterator accountIterator);
	void CheckNextDueAccount();
	void ScheduleNextCheck(const QUuid& recordId);

	WebApiTransport& sharedWebApiTransport;
	CommunityTransport& sharedCommunityTransport;
	AccountLibrary& accountLibrary;
	const SteamClock& steamClock;
	std::map<QUuid, MonitoredAccount> accounts;
	QTimer checkTimer;
};

} // namespace SteamForge
