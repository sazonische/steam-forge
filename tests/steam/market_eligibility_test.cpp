#include "steam/market_eligibility.h"
#include "support/fake_community_transport.h"

namespace {
constexpr qint64 ALLOWED_AT_SECONDS = 1893456000;
constexpr quint32 RECENT_PURCHASE_FLAG = 32;
constexpr QLatin1StringView CHECK_PATH{"market/eligibilitycheck/"};
constexpr QLatin1StringView MARKET_PATH{"market/"};
constexpr QLatin1StringView HELP_URL{"https://help.steampowered.com/ru/faqs/view/451E-96B3-D194-50FC#nottrusted"};
// The shape of the restriction list of steamcommunity.com/market/ as of 2026-10-26, with invented texts.
constexpr QLatin1StringView MARKET_PAGE{R"(<div id="BG_bottom"> <div class="market_headertip_container market_headertip_container_warning"> <div id="market_warning_header">Market unavailable because of:</div> <div id="headertooltip" class="market_restrictions"> <ul class="market_restrictions"> <li> It looks like you made a purchase less than 7 days ago.	<a href="https://help.steampowered.com/ru/faqs/view/451E-96B3-D194-50FC#nottrusted" class="tooltip trustedwaiting">Details...</a> <div id="trustedwaiting_tooltip" class="market_hover_tip_header" style="display:none">A purchase without &quot;chargebacks&quot; is needed.</div> </li> <li>Another &amp; reason <a href="https://example.invalid/help" class="tooltip">Details...</a></li> </ul> <div id="market_restriction_expire">You can use the market from <span id="market_timecanuse_header">Mon, 02 Nov 2026 07:28:26 +0000</span>.</div> </div> </div>)"};

QByteArray EligibilityCookieValue(int allowed, qint64 allowedAtSeconds) {
	const QJsonObject eligibility{
		{QStringLiteral("allowed"), allowed},
		{QStringLiteral("reason"), static_cast<qint64>(RECENT_PURCHASE_FLAG)},
		{QStringLiteral("allowed_at_time"), allowedAtSeconds},
		{QStringLiteral("steamguard_required_days"), 15},
		{QStringLiteral("new_device_cooldown_days"), 0},
	};
	return QJsonDocument(eligibility).toJson(QJsonDocument::Compact).toPercentEncoding();
}

SteamForge::CommunitySession Session() {
	return {
		.steamId = 76561198000000001,
		.accessToken = QStringLiteral("synthetic.access.token"),
		.sessionId = QStringLiteral("0123456789abcdef01234567"),
	};
}
} // namespace

class MarketEligibilityTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ReadsARestrictionFromTheCookie() {
		const std::optional<SteamForge::MarketEligibility> eligibility = SteamForge::ParseMarketEligibilityCookie(EligibilityCookieValue(0, ALLOWED_AT_SECONDS));
		QVERIFY(eligibility.has_value());
		QVERIFY(!eligibility->isAllowed);
		QCOMPARE(eligibility->allowedAt, QDateTime::fromSecsSinceEpoch(ALLOWED_AT_SECONDS, QTimeZone::UTC));
		QCOMPARE(eligibility->reasonFlags, RECENT_PURCHASE_FLAG);
	}

	void ReadsAnAllowedAccountWithoutTime() {
		const std::optional<SteamForge::MarketEligibility> eligibility = SteamForge::ParseMarketEligibilityCookie(EligibilityCookieValue(1, 0));
		QVERIFY(eligibility.has_value());
		QVERIFY(eligibility->isAllowed);
		QVERIFY(!eligibility->allowedAt.isValid());
	}

	void RefusesACookieWithoutVerdict() {
		QVERIFY(!SteamForge::ParseMarketEligibilityCookie(QByteArray("%7B%22reason%22%3A32%7D")).has_value());
		QVERIFY(!SteamForge::ParseMarketEligibilityCookie(QByteArray("not json")).has_value());
	}

	void ReadsTheReasonsOfTheMarketPage() {
		const std::vector<SteamForge::MarketRestrictionReason> reasons = SteamForge::ParseMarketRestrictionReasons(MARKET_PAGE);
		QCOMPARE(reasons.size(), std::size_t{2});
		QCOMPARE(reasons.front().text, QStringLiteral("It looks like you made a purchase less than 7 days ago."));
		QCOMPARE(reasons.front().explanation, QStringLiteral("A purchase without \"chargebacks\" is needed."));
		QCOMPARE(reasons.front().helpUrl, QUrl(HELP_URL));
		QCOMPARE(reasons.back().text, QStringLiteral("Another & reason"));
		QVERIFY(reasons.back().helpUrl.isEmpty());
	}

	void FindsNoReasonsWithoutTheList() {
		QVERIFY(SteamForge::ParseMarketRestrictionReasons(QStringLiteral("<html><body>Market</body></html>")).empty());
	}

	void ChecksTheEligibilityAndKeepsItsCookie() {
		FakeCommunityTransport transport;
		const QNetworkCookie eligibilityCookie(QByteArray("webTradeEligibility"), EligibilityCookieValue(0, ALLOWED_AT_SECONDS));
		transport.AddCookies(CHECK_PATH, QList<QNetworkCookie>{QNetworkCookie(QByteArray("browserid"), QByteArray("1")), eligibilityCookie});
		SteamForge::MarketEligibilityService service(transport);
		QObject context;
		std::optional<std::expected<SteamForge::MarketEligibilityCheck, SteamForge::WebApiError>> result;
		service.CheckEligibility(Session(), context, [&result](std::expected<SteamForge::MarketEligibilityCheck, SteamForge::WebApiError> check) {
			result = std::move(check);
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(result->has_value());
		QVERIFY(!(*result)->eligibility.isAllowed);
		QCOMPARE((*result)->eligibilityCookie.value(), eligibilityCookie.value());
		const std::vector<SteamForge::CommunityRequest> requests = transport.SentTo(CHECK_PATH);
		QCOMPARE(requests.size(), std::size_t{1});
		QCOMPARE(ParameterValue(requests.front().parameters, u"goto"), QStringLiteral("/market/"));
	}

	void RefusesACheckWithoutTheCookie() {
		FakeCommunityTransport transport;
		transport.AddCookies(CHECK_PATH, QList<QNetworkCookie>{QNetworkCookie(QByteArray("browserid"), QByteArray("1"))});
		SteamForge::MarketEligibilityService service(transport);
		QObject context;
		std::optional<std::expected<SteamForge::MarketEligibilityCheck, SteamForge::WebApiError>> result;
		service.CheckEligibility(Session(), context, [&result](std::expected<SteamForge::MarketEligibilityCheck, SteamForge::WebApiError> check) {
			result = std::move(check);
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(!result->has_value());
		QCOMPARE(result->error().kind, SteamForge::WebApiErrorKind::MALFORMED_RESPONSE);
	}

	void LoadsTheReasonsWithTheEligibilityCookie() {
		FakeCommunityTransport transport;
		transport.AddResponse(MARKET_PATH, QString(MARKET_PAGE).toUtf8());
		SteamForge::MarketEligibilityService service(transport);
		QObject context;
		std::optional<std::expected<std::vector<SteamForge::MarketRestrictionReason>, SteamForge::WebApiError>> result;
		const QNetworkCookie eligibilityCookie(QByteArray("webTradeEligibility"), EligibilityCookieValue(0, ALLOWED_AT_SECONDS));
		service.LoadRestrictionReasons(Session(), eligibilityCookie, context, [&result](std::expected<std::vector<SteamForge::MarketRestrictionReason>, SteamForge::WebApiError> reasons) {
			result = std::move(reasons);
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(result->has_value());
		QCOMPARE((*result)->size(), std::size_t{2});
		const std::vector<SteamForge::CommunityRequest> requests = transport.SentTo(MARKET_PATH);
		QCOMPARE(requests.size(), std::size_t{1});
		QCOMPARE(requests.front().extraCookies.size(), 1);
		QCOMPARE(requests.front().extraCookies.front().name(), QByteArray("webTradeEligibility"));
	}
};

QTEST_GUILESS_MAIN(MarketEligibilityTest)

#include "market_eligibility_test.moc"
