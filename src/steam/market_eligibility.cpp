#include "steam/market_eligibility.h"

#include "steam/web_api_call.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView ELIGIBILITY_CHECK_PATH{"market/eligibilitycheck/"};
	constexpr QLatin1StringView MARKET_PATH{"market/"};
	constexpr QLatin1StringView GOTO_PARAMETER{"goto"};
	constexpr QLatin1StringView GOTO_MARKET{"/market/"};
	constexpr QByteArrayView ELIGIBILITY_COOKIE_NAME{"webTradeEligibility"};
	constexpr QLatin1StringView ALLOWED_KEY{"allowed"};
	constexpr QLatin1StringView REASON_KEY{"reason"};
	constexpr QLatin1StringView ALLOWED_AT_KEY{"allowed_at_time"};
	constexpr QLatin1StringView HELP_HOST{"help.steampowered.com"};
	constexpr QLatin1StringView HTTPS_SCHEME{"https"};
	// Markup of steamcommunity.com/market/ as of 2026-10-26.
	constexpr QLatin1StringView RESTRICTION_LIST_PATTERN{R"(<ul class="market_restrictions">(.*?)</ul>)"};
	constexpr QLatin1StringView RESTRICTION_ITEM_PATTERN{R"(<li[^>]*>(.*?)</li>)"};
	constexpr QLatin1StringView HELP_LINK_PATTERN{R"pattern(<a href="([^"]+)")pattern"};
	constexpr QLatin1StringView EXPLANATION_PATTERN{R"(<div[^>]*class="market_hover_tip_header"[^>]*>(.*?)</div>)"};
	constexpr QLatin1StringView LINK_START{"<a "};
	constexpr QLatin1StringView TAG_PATTERN{R"(<[^>]*>)"};
	constexpr QLatin1StringView ENTITY_PATTERN{R"(&(#[0-9]+|#x[0-9A-Fa-f]+|[a-z]+);)"};
	constexpr int DECIMAL_BASE = 10;
	constexpr int HEXADECIMAL_BASE = 16;
	constexpr std::array NAMED_ENTITIES{
		std::pair{QLatin1StringView("amp"), QLatin1StringView("&")},
		std::pair{QLatin1StringView("lt"), QLatin1StringView("<")},
		std::pair{QLatin1StringView("gt"), QLatin1StringView(">")},
		std::pair{QLatin1StringView("quot"), QLatin1StringView("\"")},
		std::pair{QLatin1StringView("apos"), QLatin1StringView("'")},
		std::pair{QLatin1StringView("nbsp"), QLatin1StringView(" ")},
	};

	QString DecodeEntity(const QString& entity) {
		if (entity.startsWith(QLatin1Char('#'))) {
			const bool isHexadecimal = entity.startsWith(QLatin1StringView("#x"));
			const char32_t codePoint = isHexadecimal ? entity.sliced(2).toUInt(nullptr, HEXADECIMAL_BASE) : entity.sliced(1).toUInt(nullptr, DECIMAL_BASE);
			return QString::fromUcs4(&codePoint, 1);
		}
		const auto namedEntity = std::ranges::find(NAMED_ENTITIES, entity, &std::pair<QLatin1StringView, QLatin1StringView>::first);
		if (namedEntity == NAMED_ENTITIES.end()) {
			return QLatin1Char('&') + entity + QLatin1Char(';');
		}
		return namedEntity->second;
	}

	// Steam texts are short and plain, so tags become spaces and the usual entities are decoded.
	QString PlainText(const QString& html) {
		QString withoutTags = html;
		withoutTags.replace(QRegularExpression(TAG_PATTERN), QStringLiteral(" "));
		QString text;
		qsizetype copiedUntil = 0;
		QRegularExpressionMatchIterator entityIterator = QRegularExpression(ENTITY_PATTERN).globalMatch(withoutTags);
		while (entityIterator.hasNext()) {
			const QRegularExpressionMatch entityMatch = entityIterator.next();
			text += withoutTags.sliced(copiedUntil, entityMatch.capturedStart() - copiedUntil);
			text += DecodeEntity(entityMatch.captured(1));
			copiedUntil = entityMatch.capturedEnd();
		}
		text += withoutTags.sliced(copiedUntil);
		return text.simplified();
	}

	QUrl SteamHelpUrl(const QString& itemHtml) {
		const QRegularExpressionMatch linkMatch = QRegularExpression(HELP_LINK_PATTERN).match(itemHtml);
		if (!linkMatch.hasMatch()) {
			return {};
		}
		QUrl url(PlainText(linkMatch.captured(1)), QUrl::StrictMode);
		if (url.scheme() != HTTPS_SCHEME || url.host() != HELP_HOST) {
			return {};
		}
		return url;
	}
} // namespace

std::optional<MarketEligibility> ParseMarketEligibilityCookie(const QByteArray& cookieValue) {
	const QJsonDocument document = QJsonDocument::fromJson(QByteArray::fromPercentEncoding(cookieValue));
	const QJsonObject object = document.object();
	if (!object.contains(ALLOWED_KEY)) {
		return std::nullopt;
	}
	const qint64 allowedAtSeconds = object.value(ALLOWED_AT_KEY).toInteger();
	return MarketEligibility{
		.isAllowed = object.value(ALLOWED_KEY).toInteger() != 0,
		.allowedAt = allowedAtSeconds > 0 ? QDateTime::fromSecsSinceEpoch(allowedAtSeconds, QTimeZone::UTC) : QDateTime(),
		.reasonFlags = static_cast<quint32>(object.value(REASON_KEY).toInteger()),
	};
}

std::vector<MarketRestrictionReason> ParseMarketRestrictionReasons(const QString& marketPage) {
	const QRegularExpressionMatch listMatch = QRegularExpression(RESTRICTION_LIST_PATTERN, QRegularExpression::DotMatchesEverythingOption).match(marketPage);
	if (!listMatch.hasMatch()) {
		return {};
	}
	std::vector<MarketRestrictionReason> reasons;
	const QRegularExpression itemPattern(RESTRICTION_ITEM_PATTERN, QRegularExpression::DotMatchesEverythingOption);
	const QRegularExpression explanationPattern(EXPLANATION_PATTERN, QRegularExpression::DotMatchesEverythingOption);
	QRegularExpressionMatchIterator itemIterator = itemPattern.globalMatch(listMatch.captured(1));
	while (itemIterator.hasNext()) {
		const QString itemHtml = itemIterator.next().captured(1);
		const qsizetype linkStart = itemHtml.indexOf(LINK_START);
		const QString text = PlainText(linkStart >= 0 ? itemHtml.left(linkStart) : itemHtml);
		if (text.isEmpty()) {
			continue;
		}
		const QRegularExpressionMatch explanationMatch = explanationPattern.match(itemHtml);
		reasons.push_back(MarketRestrictionReason{
			.text = text,
			.explanation = explanationMatch.hasMatch() ? PlainText(explanationMatch.captured(1)) : QString(),
			.helpUrl = SteamHelpUrl(itemHtml),
		});
	}
	return reasons;
}

MarketEligibilityService::MarketEligibilityService(CommunityTransport& communityTransport) :
	transport(communityTransport) {
}

void MarketEligibilityService::CheckEligibility(const CommunitySession& session, QObject& context, WebApiHandler<MarketEligibilityCheck> handler) {
	const CommunityRequest request{
		.path = ELIGIBILITY_CHECK_PATH,
		.httpMethod = WebApiHttpMethod::GET,
		.parameters = {{GOTO_PARAMETER, GOTO_MARKET}},
		.session = session,
		.extraCookies = {},
	};
	transport.FetchCookies(request, context, [resultHandler = std::move(handler)](const CommunityCookiesResponse& response) {
		if (!response.has_value()) {
			resultHandler(std::unexpected(response.error()));
			return;
		}
		const auto cookie = std::ranges::find(*response, ELIGIBILITY_COOKIE_NAME.toByteArray(), &QNetworkCookie::name);
		// Without the cookie Steam sent the request elsewhere, for example to sign in again.
		const std::optional<MarketEligibility> eligibility = cookie != response->end() ? ParseMarketEligibilityCookie(cookie->value()) : std::nullopt;
		if (!eligibility.has_value()) {
			resultHandler(std::unexpected(MalformedWebApiResponse()));
			return;
		}
		resultHandler(MarketEligibilityCheck{
			.eligibility = *eligibility,
			.eligibilityCookie = *cookie,
		});
	});
}

void MarketEligibilityService::LoadRestrictionReasons(const CommunitySession& session, const QNetworkCookie& eligibilityCookie, QObject& context, WebApiHandler<std::vector<MarketRestrictionReason>> handler) {
	const CommunityRequest request{
		.path = MARKET_PATH,
		.httpMethod = WebApiHttpMethod::GET,
		.parameters = {},
		.session = session,
		.extraCookies = {eligibilityCookie},
	};
	transport.Send(request, context, [resultHandler = std::move(handler)](const WebApiResponse& response) {
		if (!response.has_value()) {
			resultHandler(std::unexpected(response.error()));
			return;
		}
		resultHandler(ParseMarketRestrictionReasons(QString::fromUtf8(*response)));
	});
}

} // namespace SteamForge
