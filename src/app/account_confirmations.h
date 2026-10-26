#pragma once

#include "domain/account_session.h"
#include "domain/authenticator_record.h"
#include "steam/authentication_service.h"
#include "steam/confirmation_service.h"
#include "steam/market_eligibility.h"
#include "steam/trade_offer_service.h"

namespace SteamForge {

class AccountLibrary;
class SteamClock;

enum class ConfirmationsActivity {
	IDLE,
	LOADING,
	RESPONDING
};

enum class ConfirmationsFailureKind {
	SIGN_IN_REQUIRED,
	CONNECTION,
	STEAM_ERROR,
	STORAGE_ERROR
};

struct ConfirmationsFailure {
	ConfirmationsFailureKind kind = ConfirmationsFailureKind::STEAM_ERROR;
	WebApiError error;
};

// The keys of a record that can sign confirmations, or nothing when a part of them is missing.
[[nodiscard]] std::optional<ConfirmationKeys> ConfirmationKeysOf(const AuthenticatorRecord& record);

// The mobile confirmations of one account. Its keys never change, so a list and the decisions on it
// always belong to the same account; one request runs at a time.
class AccountConfirmations final : public QObject {
	Q_OBJECT

public:
	AccountConfirmations(WebApiTransport& webApiTransport, CommunityTransport& communityTransport, AccountLibrary& library, const SteamClock& clock, QUuid accountRecordId, ConfirmationKeys accountKeys, QObject* parent = nullptr);

	// While another request runs, the list is loaded right after it.
	void Refresh();
	// Only confirmations of the current list; several at once only when all of them allow a batch response.
	[[nodiscard]] bool Respond(const QStringList& confirmationIds, ConfirmationDecision decision);

	[[nodiscard]] const QUuid& RecordId() const;
	[[nodiscard]] const ConfirmationKeys& Keys() const;
	[[nodiscard]] ConfirmationsActivity Activity() const;
	[[nodiscard]] bool HasLoaded() const;
	[[nodiscard]] const std::vector<PendingConfirmation>& Confirmations() const;
	[[nodiscard]] const QDateTime& LoadedAt() const;
	[[nodiscard]] const std::optional<ConfirmationsFailure>& LoadFailure() const;
	// The last failed decision; the list is loaded again after it, because a lost answer leaves the outcome unknown.
	[[nodiscard]] const std::optional<ConfirmationsFailure>& ResponseFailure() const;
	[[nodiscard]] bool IsResponding(const QString& confirmationId) const;
	// The items of the trade offer a trade confirmation is for, once Steam returned them.
	[[nodiscard]] const TradeOfferDetails* FindTradeOffer(const QString& offerId) const;
	// Checked at most once an hour after a loaded list; empty while the market is allowed or not yet checked.
	[[nodiscard]] const std::optional<MarketRestriction>& ActiveMarketRestriction() const;
	// After the interface language changed: loads the reasons of an active restriction again in the new one.
	void RecheckMarketRestriction();

Q_SIGNALS:
	void Changed();
	void LoadFinished();
	// Confirmations this object has not seen in an earlier list.
	void ConfirmationsArrived(const std::vector<PendingConfirmation>& newConfirmations);
	void SignInRequired();

private:
	using FailureHandler = std::function<void(const ConfirmationsFailure&)>;

	void WithFreshSession(bool isRenewalForced, FailureHandler failureHandler, std::function<void()> continuation);
	void RequestList(bool isRetryAfterRenewal);
	void SendResponse(bool isRetryAfterRenewal);
	void FinishLoad(std::expected<std::vector<PendingConfirmation>, ConfirmationsFailure> loaded);
	void FinishResponse(const std::optional<ConfirmationsFailure>& failure);
	void RequireSignIn(const ConfirmationsFailure& failure);
	void LoadNextTradeOffer();
	void CheckMarketRestriction();
	[[nodiscard]] CommunitySession CurrentCommunitySession() const;
	[[nodiscard]] std::chrono::sys_seconds CurrentSteamTime() const;
	[[nodiscard]] std::chrono::sys_seconds NextResponseTime();
	void SetActivity(ConfirmationsActivity newActivity);

	AuthenticationService authenticationService;
	ConfirmationService confirmationService;
	TradeOfferService tradeOfferService;
	MarketEligibilityService marketEligibilityService;
	AccountLibrary& accountLibrary;
	const SteamClock& steamClock;
	QUuid recordId;
	ConfirmationKeys keys;
	AccountSession session;
	QString communitySessionId;
	ConfirmationsActivity activity = ConfirmationsActivity::IDLE;
	bool hasLoaded = false;
	bool isRefreshPending = false;
	std::vector<PendingConfirmation> confirmations;
	std::vector<PendingConfirmation> respondingConfirmations;
	ConfirmationDecision respondingDecision = ConfirmationDecision::ACCEPT;
	QSet<QString> knownConfirmationIds;
	QDateTime loadedAt;
	std::optional<ConfirmationsFailure> loadFailure;
	std::optional<ConfirmationsFailure> responseFailure;
	std::chrono::sys_seconds lastResponseTime;
	QHash<QString, TradeOfferDetails> tradeOffers;
	QHash<QString, int> tradeOfferAttempts;
	// Asked once per loaded list, so a failing offer waits for the next check instead of repeating at once.
	QSet<QString> tradeOffersAskedSinceLoad;
	int tradeOfferRequestsInFlight = 0;
	std::optional<MarketRestriction> marketRestriction;
	std::optional<std::chrono::steady_clock::time_point> marketCheckedAt;
	bool isMarketCheckRunning = false;
};

} // namespace SteamForge
