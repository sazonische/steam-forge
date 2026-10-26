#include "app/account_confirmations.h"

#include "app/account_library.h"
#include "app/session_access.h"
#include "steam/steam_clock.h"

namespace SteamForge {

namespace {
	// Pictures of the items are a convenience; a trade offer Steam does not return is shown by its text.
	constexpr int MAX_TRADE_OFFER_ATTEMPTS = 2;
	constexpr int MAX_PARALLEL_TRADE_OFFER_REQUESTS = 3;
	// Restrictions last days, so an hourly check is current enough and costs Steam little.
	constexpr std::chrono::hours MARKET_CHECK_INTERVAL{1};

	ConfirmationsFailure ClassifyError(const WebApiError& error) {
		ConfirmationsFailureKind kind = ConfirmationsFailureKind::STEAM_ERROR;
		if (error.kind == WebApiErrorKind::NEEDS_AUTHENTICATION || IsSignInRequired(error)) {
			kind = ConfirmationsFailureKind::SIGN_IN_REQUIRED;
		} else if (error.kind == WebApiErrorKind::NETWORK || error.kind == WebApiErrorKind::TIMEOUT) {
			kind = ConfirmationsFailureKind::CONNECTION;
		}
		return ConfirmationsFailure{
			.kind = kind,
			.error = error,
		};
	}

	bool IsSignInRequiredFailure(const std::optional<ConfirmationsFailure>& failure) {
		return failure.has_value() && failure->kind == ConfirmationsFailureKind::SIGN_IN_REQUIRED;
	}
} // namespace

std::optional<ConfirmationKeys> ConfirmationKeysOf(const AuthenticatorRecord& record) {
	if (!DetermineCapabilities(record).canSignConfirmations) {
		return std::nullopt;
	}
	return ConfirmationKeys{
		.steamId = *record.account.steamId,
		.deviceId = record.secrets.deviceId,
		.identitySecret = record.secrets.identitySecret,
	};
}

AccountConfirmations::AccountConfirmations(WebApiTransport& webApiTransport, CommunityTransport& communityTransport, AccountLibrary& library, const SteamClock& clock, QUuid accountRecordId, ConfirmationKeys accountKeys, QObject* parent) :
	QObject(parent),
	authenticationService(webApiTransport),
	confirmationService(communityTransport),
	tradeOfferService(webApiTransport),
	marketEligibilityService(communityTransport),
	accountLibrary(library),
	steamClock(clock),
	recordId(accountRecordId),
	keys(std::move(accountKeys)),
	communitySessionId(CreateCommunitySessionId()) {
}

void AccountConfirmations::Refresh() {
	if (activity != ConfirmationsActivity::IDLE) {
		isRefreshPending = true;
		return;
	}
	isRefreshPending = false;
	SetActivity(ConfirmationsActivity::LOADING);
	RequestList(false);
}

bool AccountConfirmations::Respond(const QStringList& confirmationIds, ConfirmationDecision decision) {
	if (activity != ConfirmationsActivity::IDLE || confirmationIds.isEmpty() || IsSignInRequiredFailure(loadFailure)) {
		return false;
	}
	std::vector<PendingConfirmation> selectedConfirmations;
	for (const QString& confirmationId : confirmationIds) {
		const auto confirmationIterator = std::ranges::find(confirmations, confirmationId, &PendingConfirmation::id);
		if (confirmationIterator == confirmations.end()) {
			return false;
		}
		if (std::ranges::find(selectedConfirmations, confirmationId, &PendingConfirmation::id) == selectedConfirmations.end()) {
			selectedConfirmations.push_back(*confirmationIterator);
		}
	}
	if (selectedConfirmations.size() > 1 && !std::ranges::all_of(selectedConfirmations, &PendingConfirmation::allowsBatchResponse)) {
		return false;
	}
	responseFailure.reset();
	respondingConfirmations = std::move(selectedConfirmations);
	respondingDecision = decision;
	SetActivity(ConfirmationsActivity::RESPONDING);
	SendResponse(false);
	return true;
}

const QUuid& AccountConfirmations::RecordId() const {
	return recordId;
}

const ConfirmationKeys& AccountConfirmations::Keys() const {
	return keys;
}

ConfirmationsActivity AccountConfirmations::Activity() const {
	return activity;
}

bool AccountConfirmations::HasLoaded() const {
	return hasLoaded;
}

const std::vector<PendingConfirmation>& AccountConfirmations::Confirmations() const {
	return confirmations;
}

const QDateTime& AccountConfirmations::LoadedAt() const {
	return loadedAt;
}

const std::optional<ConfirmationsFailure>& AccountConfirmations::LoadFailure() const {
	return loadFailure;
}

const std::optional<ConfirmationsFailure>& AccountConfirmations::ResponseFailure() const {
	return responseFailure;
}

bool AccountConfirmations::IsResponding(const QString& confirmationId) const {
	return std::ranges::find(respondingConfirmations, confirmationId, &PendingConfirmation::id) != respondingConfirmations.end();
}

const TradeOfferDetails* AccountConfirmations::FindTradeOffer(const QString& offerId) const {
	const auto offerIterator = tradeOffers.constFind(offerId);
	if (offerIterator == tradeOffers.constEnd()) {
		return nullptr;
	}
	return &*offerIterator;
}

const std::optional<MarketRestriction>& AccountConfirmations::ActiveMarketRestriction() const {
	return marketRestriction;
}

void AccountConfirmations::RecheckMarketRestriction() {
	if (!marketRestriction.has_value() || marketRestriction->reasonsLanguage == InterfaceSteamLanguage()) {
		return;
	}
	marketCheckedAt.reset();
	CheckMarketRestriction();
}

void AccountConfirmations::WithFreshSession(bool isRenewalForced, FailureHandler failureHandler, std::function<void()> continuation) {
	// Read for every request: another operation of this account may have renewed the tokens meanwhile.
	std::optional<AccountSession> savedSession = accountLibrary.FindSession(recordId);
	if (!savedSession.has_value() || savedSession->steamId != keys.steamId) {
		failureHandler(ConfirmationsFailure{.kind = ConfirmationsFailureKind::SIGN_IN_REQUIRED, .error = {}});
		return;
	}
	session = std::move(*savedSession);
	auto accessHandler = [continueWithSession = std::move(continuation), fail = std::move(failureHandler)](const std::expected<void, SessionAccessFailure>& access) {
		if (access.has_value()) {
			continueWithSession();
			return;
		}
		switch (access.error().kind) {
			case SessionAccessFailureKind::SIGN_IN_REQUIRED:
				fail(ConfirmationsFailure{.kind = ConfirmationsFailureKind::SIGN_IN_REQUIRED, .error = access.error().error});
				return;
			case SessionAccessFailureKind::STORAGE_ERROR:
				fail(ConfirmationsFailure{.kind = ConfirmationsFailureKind::STORAGE_ERROR, .error = {}});
				return;
			case SessionAccessFailureKind::RESPONSE_ERROR:
				fail(ClassifyError(access.error().error));
				return;
		}
	};
	if (isRenewalForced) {
		RenewAccessToken(authenticationService, accountLibrary, session, *this, std::move(accessHandler));
	} else {
		EnsureFreshAccessToken(authenticationService, accountLibrary, session, *this, std::move(accessHandler));
	}
}

void AccountConfirmations::RequestList(bool isRetryAfterRenewal) {
	auto fail = [this](const ConfirmationsFailure& failure) {
		FinishLoad(std::unexpected(failure));
	};
	WithFreshSession(isRetryAfterRenewal, fail, [this, isRetryAfterRenewal, fail]() {
		confirmationService.GetList(CurrentCommunitySession(), keys, CurrentSteamTime(), *this, [this, isRetryAfterRenewal, fail](std::expected<std::vector<PendingConfirmation>, WebApiError> list) {
			if (!list.has_value()) {
				// Steam may revoke the access token before it expires; one renewal tells this apart from a lost sign-in.
				if (list.error().kind == WebApiErrorKind::NEEDS_AUTHENTICATION && !isRetryAfterRenewal) {
					RequestList(true);
					return;
				}
				fail(ClassifyError(list.error()));
				return;
			}
			FinishLoad(std::move(*list));
		});
	});
}

void AccountConfirmations::SendResponse(bool isRetryAfterRenewal) {
	auto fail = [this](const ConfirmationsFailure& failure) {
		FinishResponse(failure);
	};
	WithFreshSession(isRetryAfterRenewal, fail, [this, isRetryAfterRenewal, fail]() {
		confirmationService.Respond(CurrentCommunitySession(), keys, NextResponseTime(), respondingConfirmations, respondingDecision, *this, [this, isRetryAfterRenewal, fail](const std::expected<void, WebApiError>& sent) {
			if (!sent.has_value()) {
				// Steam acts on no request it did not authenticate, so sending it again cannot decide twice.
				if (sent.error().kind == WebApiErrorKind::NEEDS_AUTHENTICATION && !isRetryAfterRenewal) {
					SendResponse(true);
					return;
				}
				fail(ClassifyError(sent.error()));
				return;
			}
			FinishResponse(std::nullopt);
		});
	});
}

void AccountConfirmations::FinishLoad(std::expected<std::vector<PendingConfirmation>, ConfirmationsFailure> loaded) {
	std::vector<PendingConfirmation> newConfirmations;
	if (loaded.has_value()) {
		for (const PendingConfirmation& confirmation : *loaded) {
			if (!knownConfirmationIds.contains(confirmation.id)) {
				knownConfirmationIds.insert(confirmation.id);
				newConfirmations.push_back(confirmation);
			}
		}
		confirmations = std::move(*loaded);
		hasLoaded = true;
		loadedAt = QDateTime::currentDateTimeUtc();
		loadFailure.reset();
		const auto isListedOffer = [this](const QString& offerId) {
			return std::ranges::any_of(confirmations, [&offerId](const PendingConfirmation& confirmation) {
				return confirmation.category == ConfirmationCategory::TRADE && confirmation.creatorId == offerId;
			});
		};
		tradeOffers.removeIf([&isListedOffer](QHash<QString, TradeOfferDetails>::iterator offer) {
			return !isListedOffer(offer.key());
		});
		tradeOfferAttempts.removeIf([&isListedOffer](QHash<QString, int>::iterator attempt) {
			return !isListedOffer(attempt.key());
		});
		tradeOffersAskedSinceLoad.clear();
	} else if (loaded.error().kind == ConfirmationsFailureKind::SIGN_IN_REQUIRED) {
		RequireSignIn(loaded.error());
	} else {
		loadFailure = loaded.error();
	}
	SetActivity(ConfirmationsActivity::IDLE);
	if (!newConfirmations.empty()) {
		Q_EMIT ConfirmationsArrived(newConfirmations);
	}
	Q_EMIT LoadFinished();
	if (isRefreshPending && !IsSignInRequiredFailure(loadFailure)) {
		Refresh();
	}
	LoadNextTradeOffer();
	CheckMarketRestriction();
}

void AccountConfirmations::FinishResponse(const std::optional<ConfirmationsFailure>& failure) {
	if (!failure.has_value()) {
		std::erase_if(confirmations, [this](const PendingConfirmation& confirmation) {
			return IsResponding(confirmation.id);
		});
	} else {
		responseFailure = failure;
	}
	respondingConfirmations.clear();
	if (IsSignInRequiredFailure(failure)) {
		RequireSignIn(*failure);
		SetActivity(ConfirmationsActivity::IDLE);
		Q_EMIT LoadFinished();
		return;
	}
	SetActivity(ConfirmationsActivity::IDLE);
	// Read again after every decision: a lost answer leaves the outcome unknown, and Steam may drop related entries.
	Refresh();
}

void AccountConfirmations::RequireSignIn(const ConfirmationsFailure& failure) {
	const bool wasRequired = IsSignInRequiredFailure(loadFailure);
	loadFailure = failure;
	// Entries of a rejected sign-in cannot be decided on until the next sign-in loads them again.
	confirmations.clear();
	if (!wasRequired) {
		Q_EMIT SignInRequired();
	}
}

void AccountConfirmations::LoadNextTradeOffer() {
	if (session.accessToken.isEmpty() || IsSignInRequiredFailure(loadFailure)) {
		return;
	}
	for (const PendingConfirmation& confirmation : confirmations) {
		if (tradeOfferRequestsInFlight >= MAX_PARALLEL_TRADE_OFFER_REQUESTS) {
			return;
		}
		const QString& offerId = confirmation.creatorId;
		if (confirmation.category != ConfirmationCategory::TRADE || offerId.isEmpty() || tradeOffers.contains(offerId) || tradeOffersAskedSinceLoad.contains(offerId) || tradeOfferAttempts.value(offerId) >= MAX_TRADE_OFFER_ATTEMPTS) {
			continue;
		}
		++tradeOfferAttempts[offerId];
		tradeOffersAskedSinceLoad.insert(offerId);
		++tradeOfferRequestsInFlight;
		// The token the list was just loaded with; a stale one only costs the pictures until the next check.
		tradeOfferService.GetTradeOffer(session.accessToken, keys.steamId, offerId, *this, [this, offerId](std::expected<TradeOfferDetails, WebApiError> offer) {
			--tradeOfferRequestsInFlight;
			if (offer.has_value()) {
				tradeOffers.insert(offerId, std::move(*offer));
				Q_EMIT Changed();
			}
			LoadNextTradeOffer();
		});
	}
}

void AccountConfirmations::CheckMarketRestriction() {
	const auto now = std::chrono::steady_clock::now();
	const bool isCurrent = marketCheckedAt.has_value() && now - *marketCheckedAt < MARKET_CHECK_INTERVAL;
	if (session.accessToken.isEmpty() || IsSignInRequiredFailure(loadFailure) || isMarketCheckRunning || isCurrent) {
		return;
	}
	isMarketCheckRunning = true;
	marketCheckedAt = now;
	marketEligibilityService.CheckEligibility(CurrentCommunitySession(), *this, [this](std::expected<MarketEligibilityCheck, WebApiError> check) {
		// A failed check keeps the last known state: the next check comes in an hour.
		if (!check.has_value()) {
			isMarketCheckRunning = false;
			return;
		}
		if (check->eligibility.isAllowed) {
			isMarketCheckRunning = false;
			if (marketRestriction.has_value()) {
				marketRestriction.reset();
				Q_EMIT Changed();
			}
			return;
		}
		const QDateTime& allowedAt = check->eligibility.allowedAt;
		const QString reasonsLanguage(InterfaceSteamLanguage());
		const bool areReasonsKnown = marketRestriction.has_value() && marketRestriction->allowedAt == allowedAt && !marketRestriction->reasons.empty() && marketRestriction->reasonsLanguage == reasonsLanguage;
		if (areReasonsKnown) {
			isMarketCheckRunning = false;
			return;
		}
		marketRestriction = MarketRestriction{.allowedAt = allowedAt, .reasons = {}, .reasonsLanguage = {}};
		Q_EMIT Changed();
		marketEligibilityService.LoadRestrictionReasons(CurrentCommunitySession(), check->eligibilityCookie, *this, [this, reasonsLanguage](std::expected<std::vector<MarketRestrictionReason>, WebApiError> reasons) {
			isMarketCheckRunning = false;
			if (reasons.has_value() && marketRestriction.has_value()) {
				marketRestriction->reasons = std::move(*reasons);
				marketRestriction->reasonsLanguage = reasonsLanguage;
				Q_EMIT Changed();
			}
			// The language changed while the page was loading: the next loaded list checks again.
			if (reasonsLanguage != InterfaceSteamLanguage()) {
				marketCheckedAt.reset();
			}
		});
	});
}

CommunitySession AccountConfirmations::CurrentCommunitySession() const {
	return CommunitySession{
		.steamId = keys.steamId,
		.accessToken = session.accessToken,
		.sessionId = communitySessionId,
	};
}

std::chrono::sys_seconds AccountConfirmations::CurrentSteamTime() const {
	return std::chrono::floor<std::chrono::seconds>(steamClock.Now());
}

std::chrono::sys_seconds AccountConfirmations::NextResponseTime() {
	// Like node-steamcommunity, no two decisions are signed with the same time.
	lastResponseTime = std::max(CurrentSteamTime(), lastResponseTime + std::chrono::seconds(1));
	return lastResponseTime;
}

void AccountConfirmations::SetActivity(ConfirmationsActivity newActivity) {
	activity = newActivity;
	Q_EMIT Changed();
}

} // namespace SteamForge
