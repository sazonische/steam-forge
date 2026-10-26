#pragma once

#include "steam/web_api_transport.h"

namespace SteamForge {

// A property Steam attaches to an item description, for example its exterior or rarity.
struct ItemTag {
	QString category;
	QString name;
	QColor color;
};

// One item of a trade offer with its description, as the Steam inventory shows it.
struct TradeItem {
	QString appId;
	QString contextId;
	QString assetId;
	quint64 ownerSteamId = 0;
	QString name;
	QString marketHashName;
	QString type;
	// Rarity colour of the name and the background colour of the tile; invalid when Steam sends none.
	QColor nameColor;
	QColor backgroundColor;
	QUrl imageUrl;
	QUrl largeImageUrl;
	qint64 amount = 1;
	std::vector<ItemTag> tags;
	// Steam's own warnings about the item, for example that a name tag renamed it.
	QStringList fraudWarnings;
	bool isTradable = false;
	bool isMarketable = false;
	// The "Inspect in Game" link with the owner and asset filled in; empty when Steam gives none or it stays incomplete.
	QString inspectLink;
};

struct TradeOfferDetails {
	QString offerId;
	QString message;
	quint64 partnerSteamId = 0;
	// The account sent this offer; otherwise the partner did.
	bool isOwnOffer = false;
	QDateTime expiresAt;
	// Steam holds the items until then before they change hands.
	QDateTime holdEndsAt;
	std::vector<TradeItem> itemsToGive;
	std::vector<TradeItem> itemsToReceive;
};

[[nodiscard]] std::expected<TradeOfferDetails, WebApiError> ParseTradeOffer(const QByteArray& responseBody, quint64 ownSteamId);

// The picture of an item description on the Steam community CDN at the given edge; empty for a hash that is not one.
[[nodiscard]] QUrl EconomyImageUrl(const QString& iconHash, int edgeLength = 96);

// IEconService/GetTradeOffer of the classic Web API, called with the access token of the account.
class TradeOfferService final {
public:
	explicit TradeOfferService(WebApiTransport& transport);

	void GetTradeOffer(const QString& accessToken, quint64 ownSteamId, const QString& offerId, QObject& context, WebApiHandler<TradeOfferDetails> handler);

private:
	WebApiTransport& webApiTransport;
};

} // namespace SteamForge
