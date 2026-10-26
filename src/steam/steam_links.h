#pragma once

#include "steam/trade_offer_service.h"

namespace SteamForge {

// Pages a trader opens in the browser or in Steam to check a trade; empty when a part is missing or malformed.
[[nodiscard]] QUrl SteamProfileUrl(quint64 steamId);
[[nodiscard]] QUrl TradeOfferUrl(const QString& offerId);
[[nodiscard]] QUrl MarketListingUrl(const TradeItem& item);
[[nodiscard]] QUrl InventoryItemUrl(const TradeItem& item);

// The item code of an inspect link in the current format, which carries the item itself; the older
// S…A…D… links only ask Valve for the item and have no code.
[[nodiscard]] std::optional<QString> InspectItemCode(const QString& inspectLink);

// The page of the item on reskins.gg: the item in 3D with its name, collection, rarity and wear.
[[nodiscard]] QUrl ReskinsItemPageUrl(const QString& itemCode, bool isRussian);

} // namespace SteamForge
