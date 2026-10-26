#include "steam/trade_offer_service.h"
#include "support/fake_web_api_transport.h"

namespace {
constexpr QLatin1StringView GET_TRADE_OFFER_METHOD{"GetTradeOffer"};
constexpr QLatin1StringView GET_INVENTORY_ITEMS_METHOD{"GetInventoryItemsWithDescriptions"};
constexpr QLatin1StringView OFFER_ID{"6123456789"};
constexpr quint64 OWN_STEAM_ID = 76561198000000001;
constexpr QLatin1StringView RIFLE_ICON{"synthetic-rifle_icon-0001"};
constexpr QLatin1StringView CASE_ICON{"synthetic-case_icon-0002"};
constexpr quint64 PARTNER_ACCOUNT_ID = 40000001;
constexpr quint64 PARTNER_STEAM_ID = 76561197960265728 + PARTNER_ACCOUNT_ID;
constexpr qint64 EXPIRATION_TIME = 1893456000;
constexpr qint64 HOLD_END_TIME = 1893542400;
constexpr QLatin1StringView INSPECT_TEMPLATE{"steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20S%owner_steamid%A%assetid%D1234"};
constexpr QLatin1StringView UNRESOLVED_INSPECT_TEMPLATE{"steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20%unknown_value%"};
// The form Steam gives since March 2026: the code is the asset property 6, "Item Certificate".
constexpr QLatin1StringView PROPERTY_INSPECT_TEMPLATE{"steam://run/730//+csgo_econ_action_preview%20%propid:6%"};
constexpr QLatin1StringView OWN_SKIN_ASSET_ID{"30000000011"};
constexpr QLatin1StringView PARTNER_SKIN_ASSET_ID{"30000000012"};
// AK-47 | Redline, a code built the way reskins.gg builds them, and a second synthetic code.
constexpr QLatin1StringView OWN_ITEM_CODE{"001807209A022805300438D2F0FAF803409505D00FCBB5"};
constexpr QLatin1StringView PARTNER_ITEM_CODE{"00180720AA0228053004380B40D7076214080310B9252D00004042000000"};

QJsonObject Asset(const QString& assetId, const QString& classId, const QString& amount) {
	return {
		{QStringLiteral("appid"), 730},
		{QStringLiteral("contextid"), QStringLiteral("2")},
		{QStringLiteral("assetid"), assetId},
		{QStringLiteral("classid"), classId},
		{QStringLiteral("instanceid"), QStringLiteral("0")},
		{QStringLiteral("amount"), amount},
	};
}

QJsonObject Description(const QString& classId, const QString& name, const QString& nameColor, const QString& iconHash) {
	return {
		{QStringLiteral("appid"), 730},
		{QStringLiteral("classid"), classId},
		{QStringLiteral("instanceid"), QStringLiteral("0")},
		{QStringLiteral("name"), name},
		{QStringLiteral("type"), QStringLiteral("Синтетический тип")},
		{QStringLiteral("name_color"), nameColor},
		{QStringLiteral("background_color"), QString()},
		{QStringLiteral("icon_url"), iconHash},
	};
}

QJsonObject InspectAction(const QString& link) {
	return {
		{QStringLiteral("link"), link},
		{QStringLiteral("name"), QStringLiteral("Synthetic inspect")},
	};
}

QJsonObject RifleDescription() {
	QJsonObject description = Description(QStringLiteral("101"), QStringLiteral("Rifle | Synthetic"), QStringLiteral("eb4b4b"), RIFLE_ICON);
	description.insert(QStringLiteral("market_hash_name"), QStringLiteral("Rifle | Synthetic (Field-Tested)"));
	description.insert(QStringLiteral("tradable"), 1);
	description.insert(QStringLiteral("marketable"), true);
	description.insert(QStringLiteral("fraudwarnings"), QJsonArray{QStringLiteral("Synthetic warning")});
	const QJsonObject localizedTag{
		{QStringLiteral("category_name"), QStringLiteral("Exterior")},
		{QStringLiteral("localized_category_name"), QStringLiteral("Износ")},
		{QStringLiteral("name"), QStringLiteral("Field-Tested")},
		{QStringLiteral("localized_tag_name"), QStringLiteral("После полевых испытаний")},
		{QStringLiteral("color"), QStringLiteral("8650AC")},
	};
	const QJsonObject plainTag{
		{QStringLiteral("category_name"), QStringLiteral("Type")},
		{QStringLiteral("name"), QStringLiteral("Rifle")},
	};
	description.insert(QStringLiteral("tags"), QJsonArray{localizedTag, plainTag});
	description.insert(QStringLiteral("actions"), QJsonArray{InspectAction(QStringLiteral("https://example.invalid/not-a-game-link")), InspectAction(INSPECT_TEMPLATE)});
	return description;
}

QJsonObject CaseDescription() {
	QJsonObject description = Description(QStringLiteral("202"), QStringLiteral("Synthetic Case"), QStringLiteral("D2D2D2"), CASE_ICON);
	description.insert(QStringLiteral("tradable"), 0);
	description.insert(QStringLiteral("actions"), QJsonArray{InspectAction(UNRESOLVED_INSPECT_TEMPLATE)});
	return description;
}

QByteArray OfferResponse() {
	const QJsonObject offer{
		{QStringLiteral("tradeofferid"), OFFER_ID},
		{QStringLiteral("message"), QStringLiteral("Синтетическое сообщение")},
		{QStringLiteral("accountid_other"), static_cast<qint64>(PARTNER_ACCOUNT_ID)},
		{QStringLiteral("is_our_offer"), false},
		{QStringLiteral("expiration_time"), EXPIRATION_TIME},
		{QStringLiteral("escrow_end_date"), HOLD_END_TIME},
		{QStringLiteral("items_to_give"), QJsonArray{Asset(QStringLiteral("30000000001"), QStringLiteral("101"), QStringLiteral("1")), Asset(QStringLiteral("30000000002"), QStringLiteral("999"), QStringLiteral("1"))}},
		{QStringLiteral("items_to_receive"), QJsonArray{Asset(QStringLiteral("30000000003"), QStringLiteral("202"), QStringLiteral("3"))}},
	};
	const QJsonArray descriptions{RifleDescription(), CaseDescription()};
	const QJsonObject response{
		{QStringLiteral("offer"), offer},
		{QStringLiteral("descriptions"), descriptions},
	};
	return QJsonDocument(QJsonObject{{QStringLiteral("response"), response}}).toJson(QJsonDocument::Compact);
}
// A skin on each side whose inspect link names the asset property 6.
QByteArray PropertyOfferResponse() {
	QJsonObject skin = Description(QStringLiteral("301"), QStringLiteral("Rifle | Certified"), QStringLiteral("eb4b4b"), RIFLE_ICON);
	skin.insert(QStringLiteral("actions"), QJsonArray{InspectAction(PROPERTY_INSPECT_TEMPLATE)});
	const QJsonObject offer{
		{QStringLiteral("tradeofferid"), OFFER_ID},
		{QStringLiteral("accountid_other"), static_cast<qint64>(PARTNER_ACCOUNT_ID)},
		{QStringLiteral("is_our_offer"), false},
		{QStringLiteral("items_to_give"), QJsonArray{Asset(OWN_SKIN_ASSET_ID, QStringLiteral("301"), QStringLiteral("1"))}},
		{QStringLiteral("items_to_receive"), QJsonArray{Asset(PARTNER_SKIN_ASSET_ID, QStringLiteral("301"), QStringLiteral("1"))}},
	};
	const QJsonObject response{{QStringLiteral("offer"), offer}, {QStringLiteral("descriptions"), QJsonArray{skin}}};
	return QJsonDocument(QJsonObject{{QStringLiteral("response"), response}}).toJson(QJsonDocument::Compact);
}

QJsonObject CertificateEntry(const QString& assetId, const QString& itemCode) {
	return {
		{QStringLiteral("appid"), 730},
		{QStringLiteral("contextid"), QStringLiteral("2")},
		{QStringLiteral("assetid"), assetId},
		{QStringLiteral("asset_properties"), QJsonArray{
												 QJsonObject{{QStringLiteral("propertyid"), 1}, {QStringLiteral("int_value"), QStringLiteral("321")}},
												 QJsonObject{{QStringLiteral("propertyid"), 2}, {QStringLiteral("float_value"), 0.125}},
												 QJsonObject{{QStringLiteral("propertyid"), 6}, {QStringLiteral("string_value"), itemCode}},
											 }},
	};
}

QByteArray PropertiesResponse(const QJsonArray& entries) {
	return QJsonDocument(QJsonObject{{QStringLiteral("response"), QJsonObject{{QStringLiteral("asset_properties"), entries}}}}).toJson(QJsonDocument::Compact);
}

QJsonObject InputJsonOf(const FakeWebApiTransport::SentRequest& request) {
	if (request.jsonParameters.size() != 1 || request.jsonParameters.front().first != QStringLiteral("input_json")) {
		return {};
	}
	return QJsonDocument::fromJson(request.jsonParameters.front().second.toUtf8()).object();
}
} // namespace

class TradeOfferServiceTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ReadsBothSidesWithTheirDescriptions() {
		const auto offer = SteamForge::ParseTradeOffer(OfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		QCOMPARE(offer->offerId, QString(OFFER_ID));
		QCOMPARE(offer->message, QStringLiteral("Синтетическое сообщение"));
		QCOMPARE(offer->itemsToGive.size(), std::size_t{2});
		QCOMPARE(offer->itemsToReceive.size(), std::size_t{1});

		const SteamForge::TradeItem& rifle = offer->itemsToGive.front();
		QCOMPARE(rifle.name, QStringLiteral("Rifle | Synthetic"));
		QCOMPARE(rifle.type, QStringLiteral("Синтетический тип"));
		QCOMPARE(rifle.nameColor, QColor(0xeb, 0x4b, 0x4b));
		QVERIFY(!rifle.backgroundColor.isValid());
		QCOMPARE(rifle.imageUrl, QUrl(QStringLiteral("https://steamcommunity-a.akamaihd.net/economy/image/%1/96fx96f").arg(RIFLE_ICON)));
		QCOMPARE(offer->itemsToReceive.front().amount, qint64{3});
	}

	void ReadsThePartnerAndTheTermsOfTheOffer() {
		const auto offer = SteamForge::ParseTradeOffer(OfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		QCOMPARE(offer->partnerSteamId, PARTNER_STEAM_ID);
		QVERIFY(!offer->isOwnOffer);
		QCOMPARE(offer->expiresAt, QDateTime::fromSecsSinceEpoch(EXPIRATION_TIME, QTimeZone::UTC));
		QCOMPARE(offer->holdEndsAt, QDateTime::fromSecsSinceEpoch(HOLD_END_TIME, QTimeZone::UTC));
	}

	void GivesEachSideItsOwner() {
		const auto offer = SteamForge::ParseTradeOffer(OfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		QCOMPARE(offer->itemsToGive.front().ownerSteamId, OWN_STEAM_ID);
		QCOMPARE(offer->itemsToGive.front().assetId, QStringLiteral("30000000001"));
		QCOMPARE(offer->itemsToReceive.front().ownerSteamId, PARTNER_STEAM_ID);
	}

	void ReadsWhatATraderChecksBeforeTheDecision() {
		const auto offer = SteamForge::ParseTradeOffer(OfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		const SteamForge::TradeItem& rifle = offer->itemsToGive.front();
		QCOMPARE(rifle.marketHashName, QStringLiteral("Rifle | Synthetic (Field-Tested)"));
		QVERIFY(rifle.isTradable);
		QVERIFY(rifle.isMarketable);
		QCOMPARE(rifle.fraudWarnings, QStringList{QStringLiteral("Synthetic warning")});
		QCOMPARE(rifle.largeImageUrl, QUrl(QStringLiteral("https://steamcommunity-a.akamaihd.net/economy/image/%1/360fx360f").arg(RIFLE_ICON)));
		QCOMPARE(rifle.tags.size(), std::size_t{2});
		QCOMPARE(rifle.tags.front().category, QStringLiteral("Износ"));
		QCOMPARE(rifle.tags.front().name, QStringLiteral("После полевых испытаний"));
		QCOMPARE(rifle.tags.front().color, QColor(0x86, 0x50, 0xac));
		QCOMPARE(rifle.tags.back().category, QStringLiteral("Type"));
		QCOMPARE(rifle.tags.back().name, QStringLiteral("Rifle"));
		QVERIFY(!offer->itemsToReceive.front().isTradable);
		QVERIFY(!offer->itemsToReceive.front().isMarketable);
	}

	void ResolvesTheInspectLinkForTheOwnerAndAsset() {
		const auto offer = SteamForge::ParseTradeOffer(OfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		QCOMPARE(offer->itemsToGive.front().inspectLink, QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20S76561198000000001A30000000001D1234"));
	}

	void DropsAnInspectLinkWithUnknownPlaceholders() {
		const auto offer = SteamForge::ParseTradeOffer(OfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		QVERIFY(offer->itemsToReceive.front().inspectLink.isEmpty());
	}

	void KeepsAssetPropertyPlaceholdersForLater() {
		const auto offer = SteamForge::ParseTradeOffer(PropertyOfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		QCOMPARE(offer->itemsToGive.front().inspectLink, QString(PROPERTY_INSPECT_TEMPLATE));
		QVERIFY(SteamForge::NeedsAssetProperties(offer->itemsToGive.front()));
		QVERIFY(SteamForge::NeedsAssetProperties(offer->itemsToReceive.front()));
	}

	void ReadsTheAssetPropertiesOfEachAsset() {
		const auto properties = SteamForge::ParseAssetProperties(PropertiesResponse({CertificateEntry(OWN_SKIN_ASSET_ID, OWN_ITEM_CODE)}));
		QVERIFY(properties.has_value());
		const SteamForge::AssetProperties skin = properties->value(SteamForge::AssetPropertiesKey(QStringLiteral("730"), QStringLiteral("2"), OWN_SKIN_ASSET_ID));
		QCOMPARE(skin.value(1), QStringLiteral("321"));
		QCOMPARE(skin.value(2), QStringLiteral("0.125"));
		QCOMPARE(skin.value(6), QString(OWN_ITEM_CODE));
		QVERIFY(!SteamForge::ParseAssetProperties(QByteArray("[]")).has_value());
	}

	void FillsInspectLinksAndDropsIncompleteOnes() {
		auto offer = SteamForge::ParseTradeOffer(PropertyOfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		const auto properties = SteamForge::ParseAssetProperties(PropertiesResponse({CertificateEntry(OWN_SKIN_ASSET_ID, OWN_ITEM_CODE)}));
		QVERIFY(properties.has_value());
		SteamForge::FillInspectLinks(offer->itemsToGive, *properties);
		SteamForge::FillInspectLinks(offer->itemsToReceive, *properties);
		QCOMPARE(offer->itemsToGive.front().inspectLink, QStringLiteral("steam://run/730//+csgo_econ_action_preview%20") + OWN_ITEM_CODE);
		QVERIFY(offer->itemsToReceive.front().inspectLink.isEmpty());
	}

	void AsksForTheAssetPropertiesOfBothSides() {
		FakeWebApiTransport transport;
		transport.AddResponse(GET_TRADE_OFFER_METHOD, PropertyOfferResponse());
		const QByteArray properties = PropertiesResponse({CertificateEntry(OWN_SKIN_ASSET_ID, OWN_ITEM_CODE), CertificateEntry(PARTNER_SKIN_ASSET_ID, PARTNER_ITEM_CODE)});
		transport.AddResponse(GET_INVENTORY_ITEMS_METHOD, properties);
		transport.AddResponse(GET_INVENTORY_ITEMS_METHOD, properties);
		SteamForge::TradeOfferService service(transport);
		QObject context;
		std::optional<std::expected<SteamForge::TradeOfferDetails, SteamForge::WebApiError>> result;
		service.GetTradeOffer(QStringLiteral("synthetic.access.token"), OWN_STEAM_ID, OFFER_ID, context, [&result](std::expected<SteamForge::TradeOfferDetails, SteamForge::WebApiError> offer) {
			result = std::move(offer);
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(result->has_value());
		QCOMPARE((*result)->itemsToGive.front().inspectLink, QStringLiteral("steam://run/730//+csgo_econ_action_preview%20") + OWN_ITEM_CODE);
		QCOMPARE((*result)->itemsToReceive.front().inspectLink, QStringLiteral("steam://run/730//+csgo_econ_action_preview%20") + PARTNER_ITEM_CODE);

		const std::vector<FakeWebApiTransport::SentRequest> requests = transport.SentTo(GET_INVENTORY_ITEMS_METHOD);
		QCOMPARE(requests.size(), std::size_t{2});
		QSet<QString> askedOwners;
		for (const FakeWebApiTransport::SentRequest& request : requests) {
			QCOMPARE(request.httpMethod, SteamForge::WebApiHttpMethod::GET);
			QCOMPARE(request.accessToken, QStringLiteral("synthetic.access.token"));
			const QJsonObject input = InputJsonOf(request);
			QCOMPARE(input.value(QStringLiteral("appid")).toInt(), 730);
			QCOMPARE(input.value(QStringLiteral("contextid")).toString(), QStringLiteral("2"));
			QVERIFY(input.value(QStringLiteral("get_asset_properties")).toBool());
			QVERIFY(input.value(QStringLiteral("for_trade_offer_verification")).toBool());
			const QString owner = input.value(QStringLiteral("steamid")).toString();
			const QJsonArray assetIds = input.value(QStringLiteral("filters")).toObject().value(QStringLiteral("assetids")).toArray();
			QCOMPARE(assetIds.size(), 1);
			QCOMPARE(assetIds.first().toString(), owner == QString::number(OWN_STEAM_ID) ? QString(OWN_SKIN_ASSET_ID) : QString(PARTNER_SKIN_ASSET_ID));
			askedOwners.insert(owner);
		}
		QCOMPARE(askedOwners, (QSet<QString>{QString::number(OWN_STEAM_ID), QString::number(PARTNER_STEAM_ID)}));
	}

	void ShowsTheOfferWhenAssetPropertiesFail() {
		FakeWebApiTransport transport;
		transport.AddResponse(GET_TRADE_OFFER_METHOD, PropertyOfferResponse());
		transport.AddHttpError(GET_INVENTORY_ITEMS_METHOD, 403);
		transport.AddHttpError(GET_INVENTORY_ITEMS_METHOD, 403);
		SteamForge::TradeOfferService service(transport);
		QObject context;
		std::optional<std::expected<SteamForge::TradeOfferDetails, SteamForge::WebApiError>> result;
		service.GetTradeOffer(QStringLiteral("synthetic.access.token"), OWN_STEAM_ID, OFFER_ID, context, [&result](std::expected<SteamForge::TradeOfferDetails, SteamForge::WebApiError> offer) {
			result = std::move(offer);
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(result->has_value());
		QCOMPARE((*result)->itemsToGive.size(), std::size_t{1});
		QVERIFY((*result)->itemsToGive.front().inspectLink.isEmpty());
		QVERIFY((*result)->itemsToReceive.front().inspectLink.isEmpty());
	}

	void KeepsAnItemWithoutDescriptionWithoutPicture() {
		const auto offer = SteamForge::ParseTradeOffer(OfferResponse(), OWN_STEAM_ID);
		QVERIFY(offer.has_value());
		const SteamForge::TradeItem& unknownItem = offer->itemsToGive.back();
		QVERIFY(unknownItem.name.isEmpty());
		QVERIFY(unknownItem.imageUrl.isEmpty());
	}

	void RejectsAResponseWithoutOffer() {
		const auto offer = SteamForge::ParseTradeOffer(QByteArray(R"({"response":{}})"), OWN_STEAM_ID);
		QVERIFY(!offer.has_value());
		QCOMPARE(offer.error().kind, SteamForge::WebApiErrorKind::MALFORMED_RESPONSE);
	}

	void AcceptsOnlyHashesAsPictureNames() {
		QVERIFY(!SteamForge::EconomyImageUrl(QStringLiteral("../../login")).isValid());
		QVERIFY(!SteamForge::EconomyImageUrl(QStringLiteral("abc/def")).isValid());
		QVERIFY(!SteamForge::EconomyImageUrl(QString()).isValid());
		QVERIFY(SteamForge::EconomyImageUrl(RIFLE_ICON).isValid());
	}

	void AsksForTheOfferWithDescriptionsAndTheAccessToken() {
		FakeWebApiTransport transport;
		transport.AddResponse(GET_TRADE_OFFER_METHOD, OfferResponse());
		SteamForge::TradeOfferService service(transport);
		QObject context;
		std::optional<std::expected<SteamForge::TradeOfferDetails, SteamForge::WebApiError>> result;
		service.GetTradeOffer(QStringLiteral("synthetic.access.token"), OWN_STEAM_ID, OFFER_ID, context, [&result](std::expected<SteamForge::TradeOfferDetails, SteamForge::WebApiError> offer) {
			result = std::move(offer);
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(result->has_value());

		const std::vector<FakeWebApiTransport::SentRequest> requests = transport.SentTo(GET_TRADE_OFFER_METHOD);
		QCOMPARE(requests.size(), std::size_t{1});
		const FakeWebApiTransport::SentRequest& request = requests.front();
		QCOMPARE(request.httpMethod, SteamForge::WebApiHttpMethod::GET);
		QCOMPARE(request.accessToken, QStringLiteral("synthetic.access.token"));
		QCOMPARE(request.jsonParameters.size(), 3);
		QCOMPARE(request.jsonParameters.at(0), std::make_pair(QStringLiteral("tradeofferid"), QString(OFFER_ID)));
		QCOMPARE(request.jsonParameters.at(1), std::make_pair(QStringLiteral("get_descriptions"), QStringLiteral("1")));
		QCOMPARE(request.jsonParameters.at(2).first, QStringLiteral("language"));
		// No inspect link names asset properties, so nothing else is asked.
		QVERIFY(transport.SentTo(GET_INVENTORY_ITEMS_METHOD).empty());
	}
};

QTEST_GUILESS_MAIN(TradeOfferServiceTest)

#include "trade_offer_service_test.moc"
