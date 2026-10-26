#include "steam/trade_offer_service.h"

#include "steam/steam_http.h"
#include "steam/web_api_call.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView ECON_INTERFACE{"Econ"};
	constexpr QLatin1StringView GET_TRADE_OFFER_METHOD{"GetTradeOffer"};
	constexpr int METHOD_VERSION = 1;
	constexpr QLatin1StringView TRADE_OFFER_ID_PARAMETER{"tradeofferid"};
	constexpr QLatin1StringView DESCRIPTIONS_PARAMETER{"get_descriptions"};
	constexpr QLatin1StringView LANGUAGE_PARAMETER{"language"};
	constexpr QLatin1StringView ENABLED_VALUE{"1"};

	// COMMUNITY_CDN_URL of the Steam mobile application 3.10.9; the suffix asks for a picture of that edge.
	constexpr QLatin1StringView ECONOMY_IMAGE_PATTERN{"https://steamcommunity-a.akamaihd.net/economy/image/%1/%2fx%2f"};
	constexpr QLatin1StringView ICON_HASH_PATTERN{"^[A-Za-z0-9_-]+$"};
	constexpr int LARGE_IMAGE_EDGE = 360;
	constexpr QLatin1Char COLOR_PREFIX{'#'};
	// SteamID64 of individual accounts: this base plus the 32-bit account ID Steam reports.
	constexpr quint64 INDIVIDUAL_STEAM_ID_BASE = 76561197960265728;
	constexpr QLatin1StringView OWNER_PLACEHOLDER{"%owner_steamid%"};
	constexpr QLatin1StringView ASSET_PLACEHOLDER{"%assetid%"};
	// Lowercase only, so an escaped separator such as %20 before an uppercase argument is not taken for one.
	constexpr QLatin1StringView UNRESOLVED_PLACEHOLDER_PATTERN{"%[a-z_]+(?::\\d+)?%"};
	constexpr QLatin1StringView STEAM_LINK_PREFIX{"steam://"};
	constexpr QLatin1StringView INSPECT_ACTION_MARKER{"csgo_econ_action_preview"};

	constexpr QLatin1StringView RESPONSE_KEY{"response"};
	constexpr QLatin1StringView OFFER_KEY{"offer"};
	constexpr QLatin1StringView DESCRIPTIONS_KEY{"descriptions"};
	constexpr QLatin1StringView OFFER_ID_KEY{"tradeofferid"};
	constexpr QLatin1StringView MESSAGE_KEY{"message"};
	constexpr QLatin1StringView PARTNER_ACCOUNT_ID_KEY{"accountid_other"};
	constexpr QLatin1StringView IS_OWN_OFFER_KEY{"is_our_offer"};
	constexpr QLatin1StringView EXPIRATION_TIME_KEY{"expiration_time"};
	constexpr QLatin1StringView HOLD_END_KEY{"escrow_end_date"};
	constexpr QLatin1StringView ITEMS_TO_GIVE_KEY{"items_to_give"};
	constexpr QLatin1StringView ITEMS_TO_RECEIVE_KEY{"items_to_receive"};
	constexpr QLatin1StringView APP_ID_KEY{"appid"};
	constexpr QLatin1StringView CONTEXT_ID_KEY{"contextid"};
	constexpr QLatin1StringView ASSET_ID_KEY{"assetid"};
	constexpr QLatin1StringView CLASS_ID_KEY{"classid"};
	constexpr QLatin1StringView INSTANCE_ID_KEY{"instanceid"};
	constexpr QLatin1StringView AMOUNT_KEY{"amount"};
	constexpr QLatin1StringView NAME_KEY{"name"};
	constexpr QLatin1StringView MARKET_NAME_KEY{"market_name"};
	constexpr QLatin1StringView MARKET_HASH_NAME_KEY{"market_hash_name"};
	constexpr QLatin1StringView TYPE_KEY{"type"};
	constexpr QLatin1StringView NAME_COLOR_KEY{"name_color"};
	constexpr QLatin1StringView BACKGROUND_COLOR_KEY{"background_color"};
	constexpr QLatin1StringView ICON_URL_KEY{"icon_url"};
	constexpr QLatin1StringView LARGE_ICON_URL_KEY{"icon_url_large"};
	constexpr QLatin1StringView TAGS_KEY{"tags"};
	constexpr QLatin1StringView TAG_CATEGORY_KEY{"localized_category_name"};
	constexpr QLatin1StringView TAG_FALLBACK_CATEGORY_KEY{"category_name"};
	constexpr QLatin1StringView TAG_NAME_KEY{"localized_tag_name"};
	constexpr QLatin1StringView TAG_FALLBACK_NAME_KEY{"name"};
	constexpr QLatin1StringView TAG_COLOR_KEY{"color"};
	constexpr QLatin1StringView FRAUD_WARNINGS_KEY{"fraudwarnings"};
	constexpr QLatin1StringView TRADABLE_KEY{"tradable"};
	constexpr QLatin1StringView MARKETABLE_KEY{"marketable"};
	constexpr QLatin1StringView ACTIONS_KEY{"actions"};
	constexpr QLatin1StringView LINK_KEY{"link"};
	constexpr QLatin1StringView KEY_SEPARATOR{"_"};
	constexpr QLatin1StringView DEFAULT_INSTANCE_ID{"0"};

	// Steam sends the numbers of items either as text or as JSON numbers.
	QString ReadNumberText(const QJsonValue& value) {
		if (value.isString()) {
			return value.toString();
		}
		const qint64 number = value.toInteger(-1);
		return number >= 0 ? QString::number(number) : QString();
	}

	QDateTime ReadTime(const QJsonValue& value) {
		const qint64 seconds = ReadNumberText(value).toLongLong();
		return seconds > 0 ? QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC) : QDateTime();
	}

	// Steam marks booleans of descriptions with 1 and 0 or with true and false.
	bool ReadFlag(const QJsonValue& value) {
		return value.isBool() ? value.toBool() : value.toInteger(0) != 0;
	}

	QString DescriptionKey(const QJsonObject& object) {
		QString instanceId = ReadNumberText(object.value(INSTANCE_ID_KEY));
		if (instanceId.isEmpty()) {
			instanceId = DEFAULT_INSTANCE_ID;
		}
		return ReadNumberText(object.value(APP_ID_KEY)) + KEY_SEPARATOR + ReadNumberText(object.value(CLASS_ID_KEY)) + KEY_SEPARATOR + instanceId;
	}

	QColor ReadColor(const QJsonValue& value) {
		const QString hex = value.toString();
		if (hex.isEmpty()) {
			return {};
		}
		return QColor::fromString(COLOR_PREFIX + hex);
	}

	std::vector<ItemTag> ReadTags(const QJsonArray& tags) {
		std::vector<ItemTag> itemTags;
		for (const auto& tagValue : tags) {
			const QJsonObject tag = tagValue.toObject();
			QString category = tag.value(TAG_CATEGORY_KEY).toString();
			if (category.isEmpty()) {
				category = tag.value(TAG_FALLBACK_CATEGORY_KEY).toString();
			}
			QString name = tag.value(TAG_NAME_KEY).toString();
			if (name.isEmpty()) {
				name = tag.value(TAG_FALLBACK_NAME_KEY).toString();
			}
			if (!name.isEmpty()) {
				itemTags.push_back(ItemTag{.category = category, .name = name, .color = ReadColor(tag.value(TAG_COLOR_KEY))});
			}
		}
		return itemTags;
	}

	QStringList ReadFraudWarnings(const QJsonArray& warnings) {
		QStringList texts;
		for (const auto& warning : warnings) {
			if (warning.isString()) {
				texts.append(warning.toString());
			}
		}
		return texts;
	}

	// The game's inspect action with the item's owner and asset; a link with placeholders left is not usable.
	QString ReadInspectLink(const QJsonArray& actions, quint64 ownerSteamId, const QString& assetId) {
		for (const auto& actionValue : actions) {
			QString link = actionValue.toObject().value(LINK_KEY).toString();
			if (!link.startsWith(STEAM_LINK_PREFIX) || !link.contains(INSPECT_ACTION_MARKER)) {
				continue;
			}
			link.replace(OWNER_PLACEHOLDER, QString::number(ownerSteamId));
			link.replace(ASSET_PLACEHOLDER, assetId);
			if (link.contains(QRegularExpression(UNRESOLVED_PLACEHOLDER_PATTERN))) {
				return {};
			}
			return link;
		}
		return {};
	}

	std::vector<TradeItem> ReadItems(const QJsonArray& assets, const QHash<QString, QJsonObject>& descriptions, quint64 ownerSteamId) {
		std::vector<TradeItem> items;
		items.reserve(static_cast<std::size_t>(assets.size()));
		for (const auto& assetValue : assets) {
			const QJsonObject asset = assetValue.toObject();
			const QJsonObject description = descriptions.value(DescriptionKey(asset));
			QString name = description.value(NAME_KEY).toString();
			if (name.isEmpty()) {
				name = description.value(MARKET_NAME_KEY).toString();
			}
			const QString iconHash = description.value(ICON_URL_KEY).toString();
			QString largeIconHash = description.value(LARGE_ICON_URL_KEY).toString();
			if (largeIconHash.isEmpty()) {
				largeIconHash = iconHash;
			}
			const QString assetId = ReadNumberText(asset.value(ASSET_ID_KEY));
			const qint64 amount = ReadNumberText(asset.value(AMOUNT_KEY)).toLongLong();
			items.push_back(TradeItem{
				.appId = ReadNumberText(asset.value(APP_ID_KEY)),
				.contextId = ReadNumberText(asset.value(CONTEXT_ID_KEY)),
				.assetId = assetId,
				.ownerSteamId = ownerSteamId,
				.name = name,
				.marketHashName = description.value(MARKET_HASH_NAME_KEY).toString(),
				.type = description.value(TYPE_KEY).toString(),
				.nameColor = ReadColor(description.value(NAME_COLOR_KEY)),
				.backgroundColor = ReadColor(description.value(BACKGROUND_COLOR_KEY)),
				.imageUrl = EconomyImageUrl(iconHash),
				.largeImageUrl = EconomyImageUrl(largeIconHash, LARGE_IMAGE_EDGE),
				.amount = std::max<qint64>(amount, 1),
				.tags = ReadTags(description.value(TAGS_KEY).toArray()),
				.fraudWarnings = ReadFraudWarnings(description.value(FRAUD_WARNINGS_KEY).toArray()),
				.isTradable = ReadFlag(description.value(TRADABLE_KEY)),
				.isMarketable = ReadFlag(description.value(MARKETABLE_KEY)),
				.inspectLink = ReadInspectLink(description.value(ACTIONS_KEY).toArray(), ownerSteamId, assetId),
			});
		}
		return items;
	}
} // namespace

std::expected<TradeOfferDetails, WebApiError> ParseTradeOffer(const QByteArray& responseBody, quint64 ownSteamId) {
	const QJsonDocument document = QJsonDocument::fromJson(responseBody);
	const QJsonObject response = document.object().value(RESPONSE_KEY).toObject();
	const QJsonObject offer = response.value(OFFER_KEY).toObject();
	if (!document.isObject() || offer.isEmpty()) {
		return std::unexpected(MalformedWebApiResponse());
	}
	QHash<QString, QJsonObject> descriptions;
	for (const auto& descriptionValue : response.value(DESCRIPTIONS_KEY).toArray()) {
		const QJsonObject description = descriptionValue.toObject();
		descriptions.insert(DescriptionKey(description), description);
	}
	const quint64 partnerAccountId = ReadNumberText(offer.value(PARTNER_ACCOUNT_ID_KEY)).toULongLong();
	const quint64 partnerSteamId = partnerAccountId > 0 ? INDIVIDUAL_STEAM_ID_BASE + partnerAccountId : 0;
	return TradeOfferDetails{
		.offerId = ReadNumberText(offer.value(OFFER_ID_KEY)),
		.message = offer.value(MESSAGE_KEY).toString(),
		.partnerSteamId = partnerSteamId,
		.isOwnOffer = ReadFlag(offer.value(IS_OWN_OFFER_KEY)),
		.expiresAt = ReadTime(offer.value(EXPIRATION_TIME_KEY)),
		.holdEndsAt = ReadTime(offer.value(HOLD_END_KEY)),
		.itemsToGive = ReadItems(offer.value(ITEMS_TO_GIVE_KEY).toArray(), descriptions, ownSteamId),
		.itemsToReceive = ReadItems(offer.value(ITEMS_TO_RECEIVE_KEY).toArray(), descriptions, partnerSteamId),
	};
}

QUrl EconomyImageUrl(const QString& iconHash, int edgeLength) {
	// The hash becomes part of the path, so only the characters Steam uses in it are accepted.
	if (!QRegularExpression(ICON_HASH_PATTERN).match(iconHash).hasMatch()) {
		return {};
	}
	return QUrl(QString(ECONOMY_IMAGE_PATTERN).arg(iconHash).arg(edgeLength));
}

TradeOfferService::TradeOfferService(WebApiTransport& transport) :
	webApiTransport(transport) {
}

void TradeOfferService::GetTradeOffer(const QString& accessToken, quint64 ownSteamId, const QString& offerId, QObject& context, WebApiHandler<TradeOfferDetails> handler) {
	const WebApiRequest request{
		.interfaceName = ECON_INTERFACE,
		.methodName = GET_TRADE_OFFER_METHOD,
		.version = METHOD_VERSION,
		.httpMethod = WebApiHttpMethod::GET,
		.protobufBody = {},
		.accessToken = accessToken,
		.encoding = WebApiEncoding::JSON,
		.jsonParameters = {
			{TRADE_OFFER_ID_PARAMETER, offerId},
			{DESCRIPTIONS_PARAMETER, ENABLED_VALUE},
			{LANGUAGE_PARAMETER, InterfaceSteamLanguage()},
		},
	};
	webApiTransport.Send(request, context, [resultHandler = std::move(handler), ownSteamId](const WebApiResponse& response) {
		if (!response.has_value()) {
			resultHandler(std::unexpected(response.error()));
			return;
		}
		resultHandler(ParseTradeOffer(*response, ownSteamId));
	});
}

} // namespace SteamForge
