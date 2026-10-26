#include "app/confirmation_monitor.h"

#include "app/account_library.h"
#include "steam/jwt_claims.h"
#include "steam/steam_clock.h"

namespace SteamForge {

namespace {
	// At most one background request per tick keeps many accounts from flooding Steam Community.
	constexpr std::chrono::milliseconds CHECK_TICK{std::chrono::seconds(2)};
	constexpr std::chrono::seconds CHECK_INTERVAL{30};
	constexpr std::chrono::seconds FIRST_RETRY_DELAY{60};
	constexpr std::chrono::seconds MAX_RETRY_DELAY{std::chrono::minutes(15)};
	constexpr std::chrono::seconds FIRST_CHECK_SPACING{2};
	constexpr std::chrono::steady_clock::time_point NEVER = std::chrono::steady_clock::time_point::max();
} // namespace

ConfirmationMonitor::ConfirmationMonitor(WebApiTransport& webApiTransport, CommunityTransport& communityTransport, AccountLibrary& library, const SteamClock& clock, QObject* parent) :
	QObject(parent),
	sharedWebApiTransport(webApiTransport),
	sharedCommunityTransport(communityTransport),
	accountLibrary(library),
	steamClock(clock) {
	checkTimer.setInterval(CHECK_TICK);
	connect(&checkTimer, &QTimer::timeout, this, &ConfirmationMonitor::CheckNextDueAccount);
}

void ConfirmationMonitor::Start() {
	connect(&accountLibrary, &AccountLibrary::RecordsChanged, this, &ConfirmationMonitor::SynchronizeAccounts);
	connect(&accountLibrary, &AccountLibrary::SessionChanged, this, &ConfirmationMonitor::SynchronizeAccounts);
	SynchronizeAccounts();
	checkTimer.start();
}

AccountConfirmations* ConfirmationMonitor::Find(const QUuid& recordId) const {
	const auto accountIterator = accounts.find(recordId);
	if (accountIterator == accounts.end()) {
		return nullptr;
	}
	return accountIterator->second.confirmations;
}

void ConfirmationMonitor::RecheckMarketRestrictions() {
	for (const auto& [recordId, account] : accounts) {
		account.confirmations->RecheckMarketRestriction();
	}
}

void ConfirmationMonitor::SynchronizeAccounts() {
	const auto now = std::chrono::steady_clock::now();
	const QDateTime currentTime = QDateTime::currentDateTimeUtc();
	bool hasChanged = false;
	QSet<QUuid> monitoredRecordIds;
	std::chrono::seconds firstCheckDelay{0};
	for (const AuthenticatorRecord& record : accountLibrary.Records()) {
		const std::optional<ConfirmationKeys> keys = ConfirmationKeysOf(record);
		const std::optional<AccountSession> session = accountLibrary.FindSession(record.id);
		if (!keys.has_value() || !session.has_value() || session->steamId != keys->steamId || HasExpired(session->refreshToken, currentTime)) {
			continue;
		}
		monitoredRecordIds.insert(record.id);
		const auto accountIterator = accounts.find(record.id);
		if (accountIterator == accounts.end() || accountIterator->second.confirmations->Keys() != *keys) {
			if (accountIterator != accounts.end()) {
				RemoveAccount(accountIterator);
			}
			AddAccount(record.id, *keys, session->refreshToken, now + firstCheckDelay);
			firstCheckDelay += FIRST_CHECK_SPACING;
			hasChanged = true;
			continue;
		}
		MonitoredAccount& account = accountIterator->second;
		if (account.refreshToken != session->refreshToken) {
			account.refreshToken = session->refreshToken;
			const std::optional<ConfirmationsFailure>& failure = account.confirmations->LoadFailure();
			if (failure.has_value() && failure->kind == ConfirmationsFailureKind::SIGN_IN_REQUIRED) {
				account.retryDelay = std::chrono::seconds(0);
				account.nextCheckAt = now;
			}
		}
	}
	for (auto accountIterator = accounts.begin(); accountIterator != accounts.end();) {
		if (monitoredRecordIds.contains(accountIterator->first)) {
			++accountIterator;
			continue;
		}
		const auto removedIterator = accountIterator++;
		RemoveAccount(removedIterator);
		hasChanged = true;
	}
	if (hasChanged) {
		Q_EMIT AccountsChanged();
	}
}

void ConfirmationMonitor::AddAccount(const QUuid& recordId, const ConfirmationKeys& keys, const QString& refreshToken, std::chrono::steady_clock::time_point firstCheckAt) {
	auto* confirmations = new AccountConfirmations(sharedWebApiTransport, sharedCommunityTransport, accountLibrary, steamClock, recordId, keys, this);
	connect(confirmations, &AccountConfirmations::LoadFinished, this, [this, recordId]() {
		ScheduleNextCheck(recordId);
	});
	connect(confirmations, &AccountConfirmations::ConfirmationsArrived, this, [this, recordId](const std::vector<PendingConfirmation>& newConfirmations) {
		Q_EMIT ConfirmationsArrived(recordId, newConfirmations);
	});
	connect(confirmations, &AccountConfirmations::SignInRequired, this, [this, recordId]() {
		Q_EMIT SignInRequired(recordId);
	});
	MonitoredAccount account{
		.confirmations = confirmations,
		.refreshToken = refreshToken,
		.nextCheckAt = firstCheckAt,
		.retryDelay = std::chrono::seconds(0),
	};
	accounts.insert_or_assign(recordId, std::move(account));
}

void ConfirmationMonitor::RemoveAccount(std::map<QUuid, MonitoredAccount>::iterator accountIterator) {
	AccountConfirmations* confirmations = accountIterator->second.confirmations;
	disconnect(confirmations, nullptr, this, nullptr);
	// Deleted later: the change may be reported from inside a signal of this very object.
	confirmations->deleteLater();
	accounts.erase(accountIterator);
}

void ConfirmationMonitor::CheckNextDueAccount() {
	// Confirmation signatures carry the time; until the first synchronization the offset of this clock is unknown.
	if (steamClock.State() == SteamClockState::NOT_SYNCHRONIZED) {
		return;
	}
	const auto now = std::chrono::steady_clock::now();
	MonitoredAccount* mostOverdueAccount = nullptr;
	for (auto& [recordId, account] : accounts) {
		if (account.confirmations->Activity() != ConfirmationsActivity::IDLE || account.nextCheckAt > now) {
			continue;
		}
		if (mostOverdueAccount == nullptr || account.nextCheckAt < mostOverdueAccount->nextCheckAt) {
			mostOverdueAccount = &account;
		}
	}
	if (mostOverdueAccount == nullptr) {
		return;
	}
	mostOverdueAccount->nextCheckAt = NEVER;
	mostOverdueAccount->confirmations->Refresh();
}

void ConfirmationMonitor::ScheduleNextCheck(const QUuid& recordId) {
	const auto accountIterator = accounts.find(recordId);
	if (accountIterator == accounts.end()) {
		return;
	}
	MonitoredAccount& account = accountIterator->second;
	const auto now = std::chrono::steady_clock::now();
	const std::optional<ConfirmationsFailure>& failure = account.confirmations->LoadFailure();
	if (!failure.has_value()) {
		account.retryDelay = std::chrono::seconds(0);
		account.nextCheckAt = now + CHECK_INTERVAL;
		return;
	}
	if (failure->kind == ConfirmationsFailureKind::SIGN_IN_REQUIRED) {
		account.nextCheckAt = NEVER;
		return;
	}
	account.retryDelay = account.retryDelay == std::chrono::seconds(0) ? FIRST_RETRY_DELAY : std::min(account.retryDelay * 2, MAX_RETRY_DELAY);
	account.nextCheckAt = now + account.retryDelay;
}

} // namespace SteamForge
