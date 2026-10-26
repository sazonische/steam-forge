#include "steam/steam_links.h"

namespace {
constexpr quint64 OWNER_STEAM_ID = 76561198000000001;
// AK-47 | Redline, a code built the way reskins.gg builds them; the site opens it.
constexpr QLatin1StringView ITEM_CODE{"001807209A022805300438D2F0FAF803409505D00FCBB5"};

SteamForge::TradeItem Rifle() {
	return SteamForge::TradeItem{
		.appId = QStringLiteral("730"),
		.contextId = QStringLiteral("2"),
		.assetId = QStringLiteral("30000000001"),
		.ownerSteamId = OWNER_STEAM_ID,
		.name = QStringLiteral("Rifle | Synthetic"),
		.marketHashName = QStringLiteral("StatTrak™ Rifle | Synthetic (Field-Tested)"),
		.type = {},
		.nameColor = {},
		.backgroundColor = {},
		.imageUrl = {},
		.largeImageUrl = {},
		.amount = 1,
		.tags = {},
		.fraudWarnings = {},
		.isTradable = true,
		.isMarketable = true,
		.inspectLink = {},
	};
}
} // namespace

class SteamLinksTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void OpensProfilesAndOffersOnlyForValidIds() {
		QCOMPARE(SteamForge::SteamProfileUrl(OWNER_STEAM_ID), QUrl(QStringLiteral("https://steamcommunity.com/profiles/76561198000000001")));
		QVERIFY(SteamForge::SteamProfileUrl(0).isEmpty());
		QCOMPARE(SteamForge::TradeOfferUrl(QStringLiteral("6123456789")), QUrl(QStringLiteral("https://steamcommunity.com/tradeoffer/6123456789/")));
		QVERIFY(SteamForge::TradeOfferUrl(QStringLiteral("../login")).isEmpty());
		QVERIFY(SteamForge::TradeOfferUrl(QString()).isEmpty());
	}

	void EncodesTheMarketNameIntoThePath() {
		const QUrl marketUrl = SteamForge::MarketListingUrl(Rifle());
		QCOMPARE(marketUrl.toString(QUrl::FullyEncoded), QStringLiteral("https://steamcommunity.com/market/listings/730/StatTrak%E2%84%A2%20Rifle%20%7C%20Synthetic%20%28Field-Tested%29"));

		SteamForge::TradeItem slashedName = Rifle();
		slashedName.marketHashName = QStringLiteral("../../login");
		QCOMPARE(SteamForge::MarketListingUrl(slashedName).path(QUrl::FullyEncoded), QStringLiteral("/market/listings/730/..%2F..%2Flogin"));

		SteamForge::TradeItem unnamed = Rifle();
		unnamed.marketHashName.clear();
		QVERIFY(SteamForge::MarketListingUrl(unnamed).isEmpty());
	}

	void PointsTheInventoryAtTheOwnersAsset() {
		QCOMPARE(SteamForge::InventoryItemUrl(Rifle()), QUrl(QStringLiteral("https://steamcommunity.com/profiles/76561198000000001/inventory/#730_2_30000000001")));

		SteamForge::TradeItem ownerless = Rifle();
		ownerless.ownerSteamId = 0;
		QVERIFY(SteamForge::InventoryItemUrl(ownerless).isEmpty());

		SteamForge::TradeItem malformed = Rifle();
		malformed.assetId = QStringLiteral("1_2");
		QVERIFY(SteamForge::InventoryItemUrl(malformed).isEmpty());
	}

	void ExtractsTheItemCodeOfCurrentInspectLinks() {
		const QString escapedLink = QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20") + ITEM_CODE;
		QCOMPARE(SteamForge::InspectItemCode(escapedLink), std::optional<QString>(ITEM_CODE));
		const QString spacedLink = QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview ") + ITEM_CODE;
		QCOMPARE(SteamForge::InspectItemCode(spacedLink), std::optional<QString>(ITEM_CODE));
		// The current links of Steam start with steam://run/.
		const QString runLink = QStringLiteral("steam://run/730//+csgo_econ_action_preview%20") + ITEM_CODE;
		QCOMPARE(SteamForge::InspectItemCode(runLink), std::optional<QString>(ITEM_CODE));
	}

	// The same forms the decoder of reskins.gg accepts.
	void AcceptsTheCodesTheViewerOpens() {
		const QString lowercaseCode = QString(ITEM_CODE).toLower();
		QCOMPARE(SteamForge::InspectItemCode(QStringLiteral("steam://run/730//+csgo_econ_action_preview%20") + lowercaseCode), std::optional<QString>(lowercaseCode));
		const QString unmaskedCode = QStringLiteral("GG") + QString(ITEM_CODE).sliced(2);
		QCOMPARE(SteamForge::InspectItemCode(QStringLiteral("steam://run/730//+csgo_econ_action_preview%20") + unmaskedCode), std::optional<QString>(unmaskedCode));
		QVERIFY(!SteamForge::InspectItemCode(QStringLiteral("steam://run/730//+csgo_econ_action_preview%20") + QString(ITEM_CODE).chopped(1)).has_value());
		QVERIFY(!SteamForge::InspectItemCode(QStringLiteral("steam://run/730//+csgo_econ_action_preview%200018072A")).has_value());
	}

	void FindsNoItemCodeInLegacyLinks() {
		QVERIFY(!SteamForge::InspectItemCode(QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20S76561198000000001A30000000001D1234")).has_value());
		QVERIFY(!SteamForge::InspectItemCode(QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20M1234A30000000001D1234")).has_value());
		QVERIFY(!SteamForge::InspectItemCode(QString()).has_value());
		QVERIFY(!SteamForge::InspectItemCode(QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20") + ITEM_CODE + QStringLiteral("/../x")).has_value());
	}

	void OpensTheItemPageInTheInterfaceLanguage() {
		QCOMPARE(SteamForge::ReskinsItemPageUrl(ITEM_CODE, true), QUrl(QStringLiteral("https://reskins.gg/ru?i=") + ITEM_CODE));
		QCOMPARE(SteamForge::ReskinsItemPageUrl(ITEM_CODE, false), QUrl(QStringLiteral("https://reskins.gg/?i=") + ITEM_CODE));
		QVERIFY(SteamForge::ReskinsItemPageUrl(QStringLiteral("S1A2D3"), false).isEmpty());
	}
};

QTEST_GUILESS_MAIN(SteamLinksTest)

#include "steam_links_test.moc"
