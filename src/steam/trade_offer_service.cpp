#include "steam/trade_offer_service.h"

#include "steam/steam_http.h"
#include "steam/web_api_call.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView ECON_INTERFACE{"Econ"};
	constexpr QLatin1StringView GET_TRADE_OFFER_METHOD{"GetTradeOffer"};
	constexpr QLatin1StringView GET_INVENTORY_ITEMS_METHOD{"GetInventoryItemsWithDescriptions"};
	constexpr int METHOD_VERSION = 1;
	constexpr QLatin1StringView TRADE_OFFER_ID_PARAMETER{"tradeofferid"};
	constexpr QLatin1StringView DESCRIPTIONS_PARAMETER{"get_descriptions"};
	constexpr QLatin1StringView LANGUAGE_PARAMETER{"language"};
	constexpr QLatin1StringView ENABLED_VALUE{"1"};
	// The inventory method reads its nested asset filter only from JSON; indexed parameters fail with EResult 29.
	constexpr QLatin1StringView INPUT_JSON_PARAMETER{"input_json"};
	constexpr QLatin1StringView STEAM_ID_FIELD{"steamid"};
	constexpr QLatin1StringView GET_DESCRIPTIONS_FIELD{"get_descriptions"};
	constexpr QLatin1StringView GET_ASSET_PROPERTIES_FIELD{"get_asset_properties"};
	constexpr QLatin1StringView TRADE_OFFER_VERIFICATION_FIELD{"for_trade_offer_verification"};
	constexpr QLatin1StringView LANGUAGE_FIELD{"language"};
	constexpr QLatin1StringView FILTERS_FIELD{"filters"};
	constexpr QLatin1StringView ASSET_IDS_FIELD{"assetids"};

	// COMMUNITY_CDN_URL of the Steam mobile application 3.10.9; the suffix asks for a picture of that edge.
	constexpr QLatin1StringView ECONOMY_IMAGE_PATTERN{"https://steamcommunity-a.akamaihd.net/economy/image/%1/%2fx%2f"};
	constexpr QLatin1StringView ICON_HASH_PATTERN{"^[A-Za-z0-9_-]+$"};
	constexpr int LARGE_IMAGE_EDGE = 360;
	constexpr QLatin1Char COLOR_PREFIX{'#'};
	// SteamID64 of individual accounts: this base plus the 32-bit account ID Steam reports.
	constexpr quint64 INDIVIDUAL_STEAM_ID_BASE = 76561197960265728;
	constexpr QLatin1StringView OWNER_PLACEHOLDER{"%owner_steamid%"};
	constexpr QLatin1StringView ASSET_PLACEHOLDER{"%assetid%"};
	constexpr QLatin1StringView CONTEXT_PLACEHOLDER{"%contextid%"};
	constexpr QLatin1StringView PROPERTY_PLACEHOLDER_MARKER{"%propid:"};
	constexpr QLatin1StringView PROPERTY_PLACEHOLDER_PATTERN{"%propid:(\\d+)%"};
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
	constexpr QLatin1StringView ASSET_PROPERTIES_KEY{"asset_properties"};
	constexpr QLatin1StringView PROPERTY_ID_KEY{"propertyid"};
	constexpr QLatin1StringView FLOAT_VALUE_KEY{"float_value"};
	constexpr QLatin1StringView INT_VALUE_KEY{"int_value"};
	constexpr QLatin1StringView STRING_VALUE_KEY{"string_value"};
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

	// The game's inspect action with the item's owner, asset and context. Asset properties stay as %propid:N% for
	// FillInspectLinks; a link with any other placeholder left is not usable.
	QString ReadInspectLink(const QJsonArray& actions, quint64 ownerSteamId, const QString& contextId, const QString& assetId) {
		for (const auto& actionValue : actions) {
			QString link = actionValue.toObject().value(LINK_KEY).toString();
			if (!link.startsWith(STEAM_LINK_PREFIX) || !link.contains(INSPECT_ACTION_MARKER)) {
				continue;
			}
			link.replace(OWNER_PLACEHOLDER, QString::number(ownerSteamId));
			link.replace(ASSET_PLACEHOLDER, assetId);
			link.replace(CONTEXT_PLACEHOLDER, contextId);
			QString withoutProperties = link;
			withoutProperties.remove(QRegularExpression(PROPERTY_PLACEHOLDER_PATTERN));
			if (withoutProperties.contains(QRegularExpression(UNRESOLVED_PLACEHOLDER_PATTERN))) {
				return {};
			}
			return link;
		}
		return {};
	}

	// The value as the trade window of Steam writes it into a link: a float, else an integer, else the text.
	QString ReadPropertyValue(const QJsonObject& property) {
		if (const QJsonValue floatValue = property.value(FLOAT_VALUE_KEY); !floatValue.isUndefined()) {
			return floatValue.isString() ? floatValue.toString() : QString::number(floatValue.toDouble(), 'g', QLocale::FloatingPointShortest);
		}
		if (const QJsonValue intValue = property.value(INT_VALUE_KEY); !intValue.isUndefined()) {
			return ReadNumberText(intValue);
		}
		return property.value(STRING_VALUE_KEY).toString();
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
			const QString contextId = ReadNumberText(asset.value(CONTEXT_ID_KEY));
			const qint64 amount = ReadNumberText(asset.value(AMOUNT_KEY)).toLongLong();
			items.push_back(TradeItem{
				.appId = ReadNumberText(asset.value(APP_ID_KEY)),
				.contextId = contextId,
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
				.inspectLink = ReadInspectLink(description.value(ACTIONS_KEY).toArray(), ownerSteamId, contextId, assetId),
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

QString AssetPropertiesKey(const QString& appId, const QString& contextId, const QString& assetId) {
	return appId + KEY_SEPARATOR + contextId + KEY_SEPARATOR + assetId;
}

bool NeedsAssetProperties(const TradeItem& item) {
	return item.inspectLink.contains(PROPERTY_PLACEHOLDER_MARKER);
}

std::expected<AssetPropertiesByAsset, WebApiError> ParseAssetProperties(const QByteArray& responseBody) {
	const QJsonDocument document = QJsonDocument::fromJson(responseBody);
	const QJsonValue response = document.object().value(RESPONSE_KEY);
	if (!document.isObject() || !response.isObject()) {
		return std::unexpected(MalformedWebApiResponse());
	}
	AssetPropertiesByAsset found;
	for (const auto& entryValue : response.toObject().value(ASSET_PROPERTIES_KEY).toArray()) {
		const QJsonObject entry = entryValue.toObject();
		AssetProperties properties;
		for (const auto& propertyValue : entry.value(ASSET_PROPERTIES_KEY).toArray()) {
			const QJsonObject property = propertyValue.toObject();
			bool isNumber = false;
			const int propertyId = ReadNumberText(property.value(PROPERTY_ID_KEY)).toInt(&isNumber);
			if (isNumber) {
				properties.insert(propertyId, ReadPropertyValue(property));
			}
		}
		found.insert(AssetPropertiesKey(ReadNumberText(entry.value(APP_ID_KEY)), ReadNumberText(entry.value(CONTEXT_ID_KEY)), ReadNumberText(entry.value(ASSET_ID_KEY))), properties);
	}
	return found;
}

void FillInspectLinks(std::vector<TradeItem>& items, const AssetPropertiesByAsset& properties) {
	const QRegularExpression placeholder(PROPERTY_PLACEHOLDER_PATTERN);
	for (TradeItem& item : items) {
		if (!NeedsAssetProperties(item)) {
			continue;
		}
		const AssetProperties itemProperties = properties.value(AssetPropertiesKey(item.appId, item.contextId, item.assetId));
		QString link;
		qsizetype position = 0;
		bool isComplete = true;
		QRegularExpressionMatchIterator matches = placeholder.globalMatch(item.inspectLink);
		while (matches.hasNext()) {
			const QRegularExpressionMatch match = matches.next();
			const QString value = itemProperties.value(match.captured(1).toInt());
			isComplete = isComplete && !value.isEmpty();
			link += item.inspectLink.sliced(position, match.capturedStart() - position) + value;
			position = match.capturedEnd();
		}
		item.inspectLink = isComplete ? link + item.inspectLink.sliced(position) : QString();
	}
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
	webApiTransport.Send(request, context, [this, accessToken, ownSteamId, &context, resultHandler = std::move(handler)](const WebApiResponse& response) mutable {
		if (!response.has_value()) {
			resultHandler(std::unexpected(response.error()));
			return;
		}
		std::expected<TradeOfferDetails, WebApiError> offer = ParseTradeOffer(*response, ownSteamId);
		if (!offer.has_value()) {
			resultHandler(std::move(offer));
			return;
		}
		std::vector<AssetQuery> queries = AssetQueries(*offer);
		LoadAssetProperties(accessToken, std::move(*offer), std::move(queries), {}, context, std::move(resultHandler));
	});
}

std::vector<TradeOfferService::AssetQuery> TradeOfferService::AssetQueries(const TradeOfferDetails& offer) {
	std::vector<AssetQuery> queries;
	for (const std::vector<TradeItem>* side : {&offer.itemsToGive, &offer.itemsToReceive}) {
		for (const TradeItem& item : *side) {
			if (!NeedsAssetProperties(item) || item.ownerSteamId == 0) {
				continue;
			}
			auto query = std::ranges::find_if(queries, [&item](const AssetQuery& existing) {
				return existing.ownerSteamId == item.ownerSteamId && existing.appId == item.appId && existing.contextId == item.contextId;
			});
			if (query == queries.end()) {
				queries.push_back({.ownerSteamId = item.ownerSteamId, .appId = item.appId, .contextId = item.contextId, .assetIds = {}});
				query = std::prev(queries.end());
			}
			query->assetIds.append(item.assetId);
		}
	}
	return queries;
}

void TradeOfferService::LoadAssetProperties(QString accessToken, TradeOfferDetails offer, std::vector<AssetQuery> queries, AssetPropertiesByAsset found, QObject& context, WebApiHandler<TradeOfferDetails> handler) {
	if (queries.empty()) {
		FillInspectLinks(offer.itemsToGive, found);
		FillInspectLinks(offer.itemsToReceive, found);
		handler(std::move(offer));
		return;
	}
	const AssetQuery query = std::move(queries.back());
	queries.pop_back();
	// for_trade_offer_verification lets the account read the items of an offer in the partner's inventory.
	const QJsonObject input{
		{STEAM_ID_FIELD, QString::number(query.ownerSteamId)},
		{APP_ID_KEY, query.appId.toLongLong()},
		{CONTEXT_ID_KEY, query.contextId},
		{GET_DESCRIPTIONS_FIELD, true},
		{GET_ASSET_PROPERTIES_FIELD, true},
		{TRADE_OFFER_VERIFICATION_FIELD, true},
		{LANGUAGE_FIELD, QString(InterfaceSteamLanguage())},
		{FILTERS_FIELD, QJsonObject{{ASSET_IDS_FIELD, QJsonArray::fromStringList(query.assetIds)}}},
	};
	const WebApiRequest request{
		.interfaceName = ECON_INTERFACE,
		.methodName = GET_INVENTORY_ITEMS_METHOD,
		.version = METHOD_VERSION,
		.httpMethod = WebApiHttpMethod::GET,
		.protobufBody = {},
		.accessToken = accessToken,
		.encoding = WebApiEncoding::JSON,
		.jsonParameters = {{INPUT_JSON_PARAMETER, QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Compact))}},
	};
	webApiTransport.Send(request, context, [this, accessToken = std::move(accessToken), offer = std::move(offer), queries = std::move(queries), found = std::move(found), &context, resultHandler = std::move(handler)](const WebApiResponse& response) mutable {
		// Without the properties the offer still shows; only the inspect links of those items stay empty.
		if (response.has_value()) {
			if (std::expected<AssetPropertiesByAsset, WebApiError> properties = ParseAssetProperties(*response); properties.has_value()) {
				found.insert(*properties);
			}
		}
		LoadAssetProperties(std::move(accessToken), std::move(offer), std::move(queries), std::move(found), context, std::move(resultHandler));
	});
}

} // namespace SteamForge
