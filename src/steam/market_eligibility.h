#pragma once

#include "steam/community_transport.h"

namespace SteamForge {

// The webTradeEligibility cookie of steamcommunity.com/market/eligibilitycheck/. Its other fields
// (steamguard_required_days, new_device_cooldown_days) are rules of Steam, not restrictions in force.
struct MarketEligibility {
	bool isAllowed = false;
	// Invalid when Steam names no time.
	QDateTime allowedAt;
	// Steam publishes no meaning of these flags; the market page names the reasons in words.
	quint32 reasonFlags = 0;
};

// One item of the restriction list of the market page, in the language of the request.
struct MarketRestrictionReason {
	QString text;
	QString explanation;
	// Empty unless it leads to help.steampowered.com.
	QUrl helpUrl;
};

struct MarketRestriction {
	QDateTime allowedAt;
	std::vector<MarketRestrictionReason> reasons;
	// The Steam language the reasons were loaded in; Steam follows the Steam_Language cookie of the request.
	QString reasonsLanguage;
};

struct MarketEligibilityCheck {
	MarketEligibility eligibility;
	// Steam serves the market page only to a request that carries it.
	QNetworkCookie eligibilityCookie;
};

[[nodiscard]] std::optional<MarketEligibility> ParseMarketEligibilityCookie(const QByteArray& cookieValue);
[[nodiscard]] std::vector<MarketRestrictionReason> ParseMarketRestrictionReasons(const QString& marketPage);

class MarketEligibilityService final {
public:
	explicit MarketEligibilityService(CommunityTransport& communityTransport);

	void CheckEligibility(const CommunitySession& session, QObject& context, WebApiHandler<MarketEligibilityCheck> handler);
	// Loads the whole market page, so it is meant only for an account that is restricted.
	void LoadRestrictionReasons(const CommunitySession& session, const QNetworkCookie& eligibilityCookie, QObject& context, WebApiHandler<std::vector<MarketRestrictionReason>> handler);

private:
	CommunityTransport& transport;
};

} // namespace SteamForge
