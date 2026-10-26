#include "steam/steam_links.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView PROFILE_PATTERN{"https://steamcommunity.com/profiles/%1"};
	constexpr QLatin1StringView TRADE_OFFER_PATTERN{"https://steamcommunity.com/tradeoffer/%1/"};
	constexpr QLatin1StringView MARKET_LISTING_PATTERN{"https://steamcommunity.com/market/listings/%1/%2"};
	constexpr QLatin1StringView INVENTORY_ITEM_PATTERN{"https://steamcommunity.com/profiles/%1/inventory/#%2_%3_%4"};
	constexpr QLatin1StringView RESKINS_RUSSIAN_PATTERN{"https://reskins.gg/ru/embed/%1"};
	constexpr QLatin1StringView RESKINS_ENGLISH_PATTERN{"https://reskins.gg/embed/%1"};
	constexpr QLatin1StringView DIGITS_PATTERN{"^\\d+$"};
	constexpr QLatin1StringView ITEM_CODE_PATTERN{"^[0-9A-Fa-f]{16,}$"};
	constexpr QLatin1StringView INSPECT_ACTION_MARKER{"csgo_econ_action_preview"};
	constexpr std::array<QLatin1StringView, 3> ARGUMENT_SEPARATORS{QLatin1StringView("%20"), QLatin1StringView(" "), QLatin1StringView("+")};

	bool IsNumber(const QString& text) {
		return QRegularExpression(DIGITS_PATTERN).match(text).hasMatch();
	}
} // namespace

QUrl SteamProfileUrl(quint64 steamId) {
	if (steamId == 0) {
		return {};
	}
	return QUrl(QString(PROFILE_PATTERN).arg(steamId));
}

QUrl TradeOfferUrl(const QString& offerId) {
	if (!IsNumber(offerId)) {
		return {};
	}
	return QUrl(QString(TRADE_OFFER_PATTERN).arg(offerId));
}

QUrl MarketListingUrl(const TradeItem& item) {
	if (!IsNumber(item.appId) || item.marketHashName.isEmpty()) {
		return {};
	}
	const QString encodedName = QString::fromLatin1(QUrl::toPercentEncoding(item.marketHashName));
	return QUrl(QString(MARKET_LISTING_PATTERN).arg(item.appId, encodedName), QUrl::StrictMode);
}

QUrl InventoryItemUrl(const TradeItem& item) {
	if (item.ownerSteamId == 0 || !IsNumber(item.appId) || !IsNumber(item.contextId) || !IsNumber(item.assetId)) {
		return {};
	}
	return QUrl(QString(INVENTORY_ITEM_PATTERN).arg(QString::number(item.ownerSteamId), item.appId, item.contextId, item.assetId));
}

std::optional<QString> InspectItemCode(const QString& inspectLink) {
	const qsizetype markerPosition = inspectLink.indexOf(INSPECT_ACTION_MARKER);
	if (markerPosition < 0) {
		return std::nullopt;
	}
	QStringView argument = QStringView(inspectLink).sliced(markerPosition + INSPECT_ACTION_MARKER.size());
	for (const QLatin1StringView separator : ARGUMENT_SEPARATORS) {
		if (argument.startsWith(separator)) {
			argument = argument.sliced(separator.size());
			break;
		}
	}
	const QString code = argument.toString();
	if (!QRegularExpression(ITEM_CODE_PATTERN).match(code).hasMatch()) {
		return std::nullopt;
	}
	return code;
}

QUrl ReskinsViewerUrl(const QString& itemCode, bool isRussian) {
	if (!QRegularExpression(ITEM_CODE_PATTERN).match(itemCode).hasMatch()) {
		return {};
	}
	return QUrl(QString(isRussian ? RESKINS_RUSSIAN_PATTERN : RESKINS_ENGLISH_PATTERN).arg(itemCode));
}

} // namespace SteamForge
