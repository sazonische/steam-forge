#include "app/account_library.h"
#include "app/confirmation_monitor.h"
#include "app/interface_translator.h"
#include "platform/application_icons.h"
#include "platform/window_frame.h"
#include "steam/steam_clock.h"
#include "steam/steam_image_cache.h"
#include "steam/trade_offer_service.h"
#include "support/fake_community_transport.h"
#include "support/render_language.h"
#include "support/synthetic_tokens.h"
#include "ui/confirmation_card.h"
#include "ui/confirmations_dialog.h"
#include "ui/record_presentation.h"
#include "ui/theme.h"
#include "ui/trade_offer_dialog.h"

// A temporary demonstration for a presentation. The items are real CS2 items of the public market, listed in the file
// named by STEAMFORGE_DEMO_ITEMS, and the avatars are official avatars of the Steam avatar gallery; both pictures come
// from the Steam CDN. The partners, their nicknames and the trades are invented.
namespace {
constexpr qint64 FAR_FUTURE_SECONDS = 4102444800;
// Account numbers from 4000000000 are not issued yet, so profile and inventory links lead to no real person.
constexpr quint64 DEMO_STEAM_ID = 76561201960265729;
constexpr qint64 DEMO_PARTNER_ACCOUNT_BASE = 4000000000;
constexpr std::chrono::milliseconds DEMO_DELAY{350};
constexpr qint64 OFFER_LIFETIME_SECONDS = 14LL * 24 * 60 * 60;
constexpr qint64 HOLD_SECONDS = 7LL * 24 * 60 * 60;
constexpr QLatin1StringView AVATAR_URL_PATTERN{"https://avatars.fastly.steamstatic.com/%1_full.jpg"};
constexpr int AVATAR_WAIT_MILLISECONDS = 10000;

struct DemoItem {
	QString name;
	QString type;
	qint64 amount = 1;
	QString inspectCode;
	QStringList fraudWarnings;
	QJsonObject steamDescription;
};

struct DemoPartner {
	QString nickname;
	QLatin1StringView avatarHash;
};

struct TradeDetails {
	QStringList warnings{};
	QString message{};
	qint64 holdSeconds = 0;
};

// Steam sends these in the language of the account, so they follow the rendered interface language.
struct DemoTexts {
	QString tradeOffer;
	QString marketListing;
	QString marketPrice;
	QString itemsToGive;
	QString itemsToReceive;
	QString exterior;
	QString inspectInGame;
	QString birthdayMessage;
	QStringList scamWarnings;
	QString scamMessage;
	QString nameTag;
	QString apiKeyCreation;
	QString apiKeyDomain;
	QString accountAction;
	QString accountActionWarning;
};

[[nodiscard]] DemoTexts DemoTextsFor(SteamForge::InterfaceLanguage language) {
	if (language == SteamForge::InterfaceLanguage::ENGLISH) {
		return DemoTexts{
			.tradeOffer = QStringLiteral("Trade Offer"),
			.marketListing = QStringLiteral("Market Listing"),
			.marketPrice = QStringLiteral("Current Community Market price: %1"),
			.itemsToGive = QStringLiteral("Items you give: %1"),
			.itemsToReceive = QStringLiteral("Items you receive: %1"),
			.exterior = QStringLiteral("Exterior"),
			.inspectInGame = QStringLiteral("Inspect in Game..."),
			.birthdayMessage = QStringLiteral("Happy birthday, bro!"),
			.scamWarnings = {QStringLiteral("You are not receiving anything in return."), QStringLiteral("The partner's account was created less than 30 days ago.")},
			.scamMessage = QStringLiteral("send it for a check, will give it back in 5 min"),
			.nameTag = QStringLiteral("Name Tag: ''Gift for my grandson''"),
			.apiKeyCreation = QStringLiteral("Creating a Web API key"),
			.apiKeyDomain = QStringLiteral("Domain: example.com"),
			.accountAction = QStringLiteral("Confirm an account action"),
			.accountActionWarning = QStringLiteral("Confirm only if you started this action yourself."),
		};
	}
	return DemoTexts{
		.tradeOffer = QStringLiteral("Предложение обмена"),
		.marketListing = QStringLiteral("Продажа на Торговой площадке"),
		.marketPrice = QStringLiteral("Цена на Торговой площадке сейчас: %1"),
		.itemsToGive = QStringLiteral("Вы отдадите предметов: %1"),
		.itemsToReceive = QStringLiteral("Вы получите предметов: %1"),
		.exterior = QStringLiteral("Износ"),
		.inspectInGame = QStringLiteral("Осмотреть в игре…"),
		.birthdayMessage = QStringLiteral("С днём рождения, бро!"),
		.scamWarnings = {QStringLiteral("Вы ничего не получаете взамен."), QStringLiteral("Аккаунт партнёра создан менее 30 дней назад.")},
		.scamMessage = QStringLiteral("скинь на проверку, через 5 минут верну"),
		.nameTag = QStringLiteral("Ярлык с именем: ''Подарок внуку''"),
		.apiKeyCreation = QStringLiteral("Создание ключа Web API"),
		.apiKeyDomain = QStringLiteral("Домен: example.com"),
		.accountAction = QStringLiteral("Подтвердите действие с аккаунтом"),
		.accountActionWarning = QStringLiteral("Подтверждайте, только если начали это действие сами."),
	};
}

class DemoWorld {
public:
	DemoWorld(const QJsonArray& steamItems, DemoTexts demoTexts) :
		texts(std::move(demoTexts)) {
		for (const QJsonValue& itemValue : steamItems) {
			const QJsonObject item = itemValue.toObject();
			itemsByHashName.insert(item.value(QStringLiteral("market_hash_name")).toString(), item);
		}
		AddEntries();
	}

	[[nodiscard]] QByteArray ListResponse() const {
		QJsonArray entries;
		for (const QJsonObject& entry : confirmations) {
			entries.append(entry);
		}
		return QJsonDocument(QJsonObject{{QStringLiteral("success"), true}, {QStringLiteral("conf"), entries}}).toJson(QJsonDocument::Compact);
	}

	void Remove(const QStringList& confirmationIds) {
		std::erase_if(confirmations, [&confirmationIds](const QJsonObject& entry) {
			return confirmationIds.contains(entry.value(QStringLiteral("id")).toString());
		});
	}

	[[nodiscard]] QByteArray OfferResponse(const QString& offerId) const {
		return offers.value(offerId);
	}

	[[nodiscard]] QString ConfirmationIdOf(const QString& partner) const {
		const auto entry = std::ranges::find_if(confirmations, [&partner](const QJsonObject& confirmation) {
			return confirmation.value(QStringLiteral("headline")).toString() == partner;
		});
		return entry != confirmations.end() ? entry->value(QStringLiteral("id")).toString() : QString();
	}

	[[nodiscard]] const QStringList& MissingItems() const {
		return missingItems;
	}

private:
	[[nodiscard]] DemoItem Item(const QString& marketHashName, qint64 amount = 1, const QStringList& fraudWarnings = {}) {
		const auto found = itemsByHashName.constFind(marketHashName);
		if (found == itemsByHashName.constEnd()) {
			missingItems.append(marketHashName);
			return {};
		}
		return DemoItem{
			.name = found->value(QStringLiteral("name")).toString(),
			.type = found->value(QStringLiteral("type")).toString(),
			.amount = amount,
			.inspectCode = found->value(QStringLiteral("inspect_code")).toString(),
			.fraudWarnings = fraudWarnings,
			.steamDescription = *found,
		};
	}

	[[nodiscard]] std::vector<DemoItem> Items(const QStringList& marketHashNames) {
		std::vector<DemoItem> items;
		for (const QString& marketHashName : marketHashNames) {
			items.push_back(Item(marketHashName));
		}
		return items;
	}

	// Trades of ordinary players: a rifle for a rifle, cases for a sticker, a birthday gift, the classic "send it for a
	// check" scam that Steam warns about, and a trade held for a week.
	void AddEntries() {
		AddTrade({QStringLiteral("NightFox"), QLatin1StringView("1c31025a2426e7765d70d3a74dfb2c85c3eb8f52")}, 20, {Item(QStringLiteral("AK-47 | Redline (Battle-Scarred)"))}, {Item(QStringLiteral("M4A4 | Temukau (Field-Tested)"))});
		AddTrade({QStringLiteral("dumpling"), QLatin1StringView("744b107247e2561eef8358f7b65dafca7dc882ba")}, 150, {Item(QStringLiteral("Dreams & Nightmares Case"), 3)}, {Item(QStringLiteral("Sticker | Great Wave (Holo)"))});
		AddTrade({QStringLiteral("pixelboy"), QLatin1StringView("19804fb70c1c26945b51a4a12b9bb37febf385e6")}, 240, {}, {Item(QStringLiteral("Glock-18 | Fade (Factory New)"))}, {.message = texts.birthdayMessage});
		const std::vector<DemoItem> wholeInventory = Items({
			QStringLiteral("AK-47 | Nightwish (Field-Tested)"),
			QStringLiteral("StatTrak™ AK-47 | Redline (Battle-Scarred)"),
			QStringLiteral("AWP | Asiimov (Field-Tested)"),
			QStringLiteral("M4A1-S | Hyper Beast (Field-Tested)"),
			QStringLiteral("Desert Eagle | Blaze (Factory New)"),
			QStringLiteral("USP-S | Jawbreaker (Field-Tested)"),
			QStringLiteral("AK-47 | Crane Flight (Field-Tested)"),
			QStringLiteral("StatTrak™ FAMAS | Rapid Eye Movement (Minimal Wear)"),
			QStringLiteral("Glove Case"),
			QStringLiteral("Operation Breakout Weapon Case"),
			QStringLiteral("Sticker | PGL (Holo) | Krakow 2017"),
			QStringLiteral("Sticker | Hiko | Cologne 2016"),
			QStringLiteral("Lt. Commander Ricksaw | NSWC SEAL"),
			QStringLiteral("Crasswater The Forgotten | Guerrilla Warfare"),
		});
		const TradeDetails scam{
			.warnings = texts.scamWarnings,
			.message = texts.scamMessage,
		};
		AddTrade({QStringLiteral("lucky.exe"), QLatin1StringView("64277216e88569d0156f0b0e176413c5824c0a8f")}, 35, wholeInventory, {}, scam);
		AddListing(QStringLiteral("AK-47 | Nightwish (Field-Tested)"), 75);
		AddTrade({QStringLiteral("rush_b_no_stop"), QLatin1StringView("95ab2c6b21c6d6f00dbcbb87651f162deb541f5d")}, 400, Items({QStringLiteral("USP-S | Jawbreaker (Field-Tested)"), QStringLiteral("P250 | Visions (Minimal Wear)")}), {Item(QStringLiteral("AWP | Asiimov (Field-Tested)"))}, {.holdSeconds = HOLD_SECONDS});
		AddListing(QStringLiteral("Glove Case"), 600);
		AddTrade({QStringLiteral("Alex"), QLatin1StringView("d8325b5d409277f0d59aaf66b09780a159ce922b")}, 1200, Items({QStringLiteral("AK-47 | Slate (Field-Tested)"), QStringLiteral("AK-47 | Midnight Laminate (Field-Tested)")}), {Item(QStringLiteral("M4A1-S | Hyper Beast (Field-Tested)"))});
		std::vector<DemoItem> stickersAndCases = Items({
			QStringLiteral("Sticker | Evil Geniuses (Holo) | 2020 RMR"),
			QStringLiteral("Sticker | Sand Dune Baja Tour"),
			QStringLiteral("Sticker | Entropiq | Stockholm 2021"),
			QStringLiteral("Sticker | Cedar Creek (Holo)"),
			QStringLiteral("Sticker | Run Boost Lift Kits"),
			QStringLiteral("Sticker | Leaving The Station (Holo)"),
			QStringLiteral("Sticker | Roentgen Raceclub"),
			QStringLiteral("Sticker | Hiko | Cologne 2016"),
		});
		stickersAndCases.push_back(Item(QStringLiteral("Revolution Case"), 3));
		AddTrade({QStringLiteral("moonwalker"), QLatin1StringView("e05c4f7d7cc15acb30c2b164041f1a0c5ba02570")}, 1500, Items({QStringLiteral("StatTrak™ FAMAS | Rapid Eye Movement (Minimal Wear)"), QStringLiteral("AK-47 | Crane Flight (Field-Tested)"), QStringLiteral("Desert Eagle | Blaze (Factory New)")}), stickersAndCases);
		AddOther(texts.apiKeyCreation, texts.apiKeyDomain, 1800);
		AddTrade({QStringLiteral("Annie"), QLatin1StringView("a02473621dc127cad5de6966b37161538d8f0aa1")}, 2400, {Item(QStringLiteral("Spectrum Case"), 2)}, {Item(QStringLiteral("Operator | FBI SWAT"))});
		AddListing(QStringLiteral("Sticker | PGL (Holo) | Krakow 2017"), 2700);
		AddTrade({QStringLiteral("grandpa_gamer"), QLatin1StringView("02dab6214d59925dd5066e2ccc60701ca730fe94")}, 3000, {Item(QStringLiteral("CS:GO Weapon Case"), 2)}, {Item(QStringLiteral("★ Classic Knife | Crimson Web (Factory New)"), 1, {texts.nameTag})});
		AddListing(QStringLiteral("Operation Breakout Weapon Case"), 3600);
	}

	void AddListing(const QString& marketHashName, qint64 ageSeconds) {
		const DemoItem item = Item(marketHashName);
		const QString price = item.steamDescription.value(QStringLiteral("price_text")).toString();
		const QUrl iconUrl = SteamForge::EconomyImageUrl(item.steamDescription.value(QStringLiteral("icon_url")).toString());
		AddEntry(3, texts.marketListing, item.name, {texts.marketPrice.arg(price)}, {}, iconUrl, ageSeconds, QString::number(++nextOfferId));
	}

	QJsonArray AddItems(const std::vector<DemoItem>& items, QJsonArray& descriptions) {
		QJsonArray assets;
		for (const DemoItem& item : items) {
			assets.append(AddItem(item, descriptions));
		}
		return assets;
	}

	// Real descriptions of the market search carry no tags and no inspect action; only the exterior in the name is
	// shown, and the items with an item code built the way reskins.gg builds it get an inspect link.
	QJsonObject AddItem(const DemoItem& item, QJsonArray& descriptions) {
		const QJsonObject& steamDescription = item.steamDescription;
		const QString classId = steamDescription.value(QStringLiteral("classid")).toString();
		QJsonArray actions;
		if (!item.inspectCode.isEmpty()) {
			actions.append(QJsonObject{
				{QStringLiteral("name"), texts.inspectInGame},
				{QStringLiteral("link"), QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20") + item.inspectCode},
			});
		}
		QJsonArray tags;
		const qsizetype exteriorStart = item.name.lastIndexOf(QLatin1Char('('));
		if (exteriorStart > 0 && item.name.endsWith(QLatin1Char(')'))) {
			tags.append(QJsonObject{{QStringLiteral("localized_category_name"), texts.exterior}, {QStringLiteral("localized_tag_name"), item.name.mid(exteriorStart + 1).chopped(1)}});
		}
		descriptions.append(QJsonObject{
			{QStringLiteral("appid"), 730},
			{QStringLiteral("classid"), classId},
			{QStringLiteral("instanceid"), QStringLiteral("0")},
			{QStringLiteral("name"), item.name},
			{QStringLiteral("market_hash_name"), steamDescription.value(QStringLiteral("market_hash_name"))},
			{QStringLiteral("type"), item.type},
			{QStringLiteral("name_color"), steamDescription.value(QStringLiteral("name_color"))},
			{QStringLiteral("background_color"), steamDescription.value(QStringLiteral("background_color"))},
			{QStringLiteral("icon_url"), steamDescription.value(QStringLiteral("icon_url"))},
			{QStringLiteral("icon_url_large"), steamDescription.value(QStringLiteral("icon_url_large"))},
			{QStringLiteral("tradable"), 1},
			{QStringLiteral("marketable"), 1},
			{QStringLiteral("tags"), tags},
			{QStringLiteral("fraudwarnings"), QJsonArray::fromStringList(item.fraudWarnings)},
			{QStringLiteral("actions"), actions},
		});
		return QJsonObject{{QStringLiteral("appid"), 730}, {QStringLiteral("contextid"), QStringLiteral("2")}, {QStringLiteral("assetid"), QString::number(++nextAssetId)}, {QStringLiteral("classid"), classId}, {QStringLiteral("instanceid"), QStringLiteral("0")}, {QStringLiteral("amount"), QString::number(item.amount)}};
	}

	QString AddEntry(int type, const QString& typeName, const QString& headline, const QStringList& summary, const QStringList& warnings, const QUrl& icon, qint64 ageSeconds, const QString& creatorId) {
		const QString id = QString::number(++nextConfirmationId);
		confirmations.push_back({
			{QStringLiteral("id"), id},
			{QStringLiteral("nonce"), id + QStringLiteral("77")},
			{QStringLiteral("creator_id"), creatorId},
			{QStringLiteral("type"), type},
			{QStringLiteral("type_name"), typeName},
			{QStringLiteral("headline"), headline},
			{QStringLiteral("summary"), QJsonArray::fromStringList(summary)},
			{QStringLiteral("warn"), QJsonArray::fromStringList(warnings)},
			{QStringLiteral("icon"), icon.toString()},
			{QStringLiteral("multi"), type != 6},
			{QStringLiteral("creation_time"), QDateTime::currentSecsSinceEpoch() - ageSeconds},
		});
		return id;
	}

	void AddTrade(const DemoPartner& partner, qint64 ageSeconds, const std::vector<DemoItem>& give, const std::vector<DemoItem>& receive, const TradeDetails& details = {}) {
		const QUrl avatarUrl(QString(AVATAR_URL_PATTERN).arg(partner.avatarHash));
		const QString offerId = QString::number(++nextOfferId);
		QJsonArray descriptions;
		const QJsonObject offer{
			{QStringLiteral("tradeofferid"), offerId},
			{QStringLiteral("message"), details.message},
			{QStringLiteral("accountid_other"), DEMO_PARTNER_ACCOUNT_BASE + static_cast<qint64>(qHash(partner.nickname) % 1000000)},
			{QStringLiteral("is_our_offer"), false},
			{QStringLiteral("expiration_time"), QDateTime::currentSecsSinceEpoch() + OFFER_LIFETIME_SECONDS - ageSeconds},
			{QStringLiteral("escrow_end_date"), details.holdSeconds > 0 ? QDateTime::currentSecsSinceEpoch() + details.holdSeconds : 0},
			{QStringLiteral("items_to_give"), AddItems(give, descriptions)},
			{QStringLiteral("items_to_receive"), AddItems(receive, descriptions)},
		};
		offers.insert(offerId, QJsonDocument(QJsonObject{{QStringLiteral("response"), QJsonObject{{QStringLiteral("offer"), offer}, {QStringLiteral("descriptions"), descriptions}}}}).toJson(QJsonDocument::Compact));
		const QStringList summary{texts.itemsToGive.arg(give.size()), texts.itemsToReceive.arg(receive.size())};
		AddEntry(2, texts.tradeOffer, partner.nickname, summary, details.warnings, avatarUrl, ageSeconds, offerId);
	}

	void AddOther(const QString& typeName, const QString& summary, qint64 ageSeconds) {
		AddEntry(6, typeName, texts.accountAction, {summary}, {texts.accountActionWarning}, {}, ageSeconds, {});
	}

	DemoTexts texts;
	QHash<QString, QJsonObject> itemsByHashName;
	QStringList missingItems;
	std::vector<QJsonObject> confirmations;
	QHash<QString, QByteArray> offers;
	int nextConfirmationId = 1000;
	int nextOfferId = 6100;
	qint64 nextAssetId = 31000000000;
};

// Answers like Steam Community after a short delay; decided confirmations leave the list.
class DemoCommunityTransport final : public SteamForge::CommunityTransport {
public:
	explicit DemoCommunityTransport(DemoWorld& demoWorld) :
		world(demoWorld) {
	}

	void FetchCookies(const SteamForge::CommunityRequest& /*request*/, QObject& context, SteamForge::CommunityCookiesHandler handler) override {
		QTimer::singleShot(DEMO_DELAY, &context, [cookiesHandler = std::move(handler)]() {
			cookiesHandler(QList<QNetworkCookie>());
		});
	}

	void Send(const SteamForge::CommunityRequest& request, QObject& context, SteamForge::WebApiResponseHandler handler) override {
		QByteArray body = R"({"success":true})";
		if (request.path.endsWith(QStringLiteral("getlist"))) {
			body = world.ListResponse();
		} else {
			QStringList ids = ParameterValues(request.parameters, u"cid[]");
			ids.append(ParameterValues(request.parameters, u"cid"));
			world.Remove(ids);
		}
		QTimer::singleShot(DEMO_DELAY, &context, [responseHandler = std::move(handler), body]() {
			responseHandler(body);
		});
	}

private:
	DemoWorld& world;
};

class DemoWebApiTransport final : public SteamForge::WebApiTransport {
public:
	explicit DemoWebApiTransport(DemoWorld& demoWorld) :
		world(demoWorld) {
	}

	void Send(const SteamForge::WebApiRequest& request, QObject& context, SteamForge::WebApiResponseHandler handler) override {
		const QByteArray body = world.OfferResponse(ParameterValue(request.jsonParameters, u"tradeofferid"));
		QTimer::singleShot(DEMO_DELAY * 2, &context, [responseHandler = std::move(handler), body]() {
			responseHandler(body);
		});
	}

private:
	DemoWorld& world;
};
} // namespace

class ConfirmationsDemoTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void Demonstrate() {
		const QString renderDirectory = qEnvironmentVariable("STEAMFORGE_RENDER_DIRECTORY");
		const bool isInteractive = qEnvironmentVariableIsSet("STEAMFORGE_DEMO");
		const QString steamItemsPath = qEnvironmentVariable("STEAMFORGE_DEMO_ITEMS");
		if ((renderDirectory.isEmpty() && !isInteractive) || steamItemsPath.isEmpty()) {
			QSKIP("Set STEAMFORGE_DEMO_ITEMS to the list of real items, and STEAMFORGE_DEMO or STEAMFORGE_RENDER_DIRECTORY.");
		}
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		QApplication::setWindowIcon(SteamForge::ApplicationIcon());
		SteamForge::ApplyTheme(application);
		SteamForge::InstallWindowFrameStyling(application);
		SteamForge::InterfaceTranslator translator;
		const SteamForge::InterfaceLanguage language = RenderLanguage();
		QVERIFY(translator.Apply(language));

		const QTemporaryDir dataDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(QDir(dataDirectory.path()).filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(QDir(dataDirectory.path()).filePath(QStringLiteral("sessions"))));
		SteamForge::AuthenticatorRecord record;
		record.id = QUuid::createUuid();
		record.account.steamId = DEMO_STEAM_ID;
		record.account.accountName = QStringLiteral("artie2008");
		record.createdAt = QDateTime::currentDateTimeUtc();
		record.secrets.sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
		record.secrets.identitySecret = QByteArray::fromBase64("FBMSERAPDg0MCwoJCAcGBQQDAgE=");
		record.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-000000000001");
		QVERIFY(library.Add(record).has_value());
		QVERIFY(library.SaveSession({.recordId = record.id, .steamId = DEMO_STEAM_ID, .accountName = record.account.accountName, .refreshToken = SyntheticJwt(DEMO_STEAM_ID, {QStringLiteral("derive")}, FAR_FUTURE_SECONDS), .accessToken = SyntheticJwt(DEMO_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS), .signedInAt = QDateTime::currentDateTimeUtc()}).has_value());

		QFile steamItemsFile(steamItemsPath);
		QVERIFY(steamItemsFile.open(QIODevice::ReadOnly));
		const QJsonArray steamItems = QJsonDocument::fromJson(steamItemsFile.readAll()).array();
		QVERIFY(!steamItems.isEmpty());
		SteamForge::SteamImageCache imageCache;
		DemoWorld world(steamItems, DemoTextsFor(language));
		QVERIFY2(world.MissingItems().isEmpty(), qPrintable(world.MissingItems().join(QStringLiteral(", "))));
		DemoCommunityTransport communityTransport(world);
		DemoWebApiTransport webApiTransport(world);
		SteamForge::SteamClock clock;
		SteamForge::ConfirmationMonitor monitor(webApiTransport, communityTransport, library, clock);
		monitor.Start();

		// The window names the account by its Steam nickname and shows its avatar, as the program does.
		const QUrl accountAvatarUrl(QString(AVATAR_URL_PATTERN).arg(QLatin1StringView("4264b561c93fcd0d4c832c1137229587bbb5f8d7")));
		QImage accountAvatar = imageCache.Image(accountAvatarUrl);
		if (accountAvatar.isNull()) {
			QSignalSpy avatarLoaded(&imageCache, &SteamForge::SteamImageCache::Loaded);
			avatarLoaded.wait(AVATAR_WAIT_MILLISECONDS);
			accountAvatar = imageCache.Image(accountAvatarUrl);
		}
		SteamForge::ConfirmationsDialog dialog(monitor, imageCache, record, QStringLiteral("Artie"), accountAvatar);
		dialog.show();
		if (isInteractive) {
			QEventLoop untilClosed;
			connect(&dialog, &QDialog::finished, &untilClosed, &QEventLoop::quit);
			untilClosed.exec();
			return;
		}
		// Pictures come from the Steam CDN one by one.
		QTest::qWait(15000);
		auto* scrollArea = dialog.findChild<QScrollArea*>();
		QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("demo_top.png"))));
		scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum() / 3);
		QTest::qWait(1500);
		QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("demo_middle.png"))));
		scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum() * 2 / 3);
		QTest::qWait(1500);
		QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("demo_lower.png"))));
		scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
		QTest::qWait(1500);
		QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("demo_bottom.png"))));

		const std::array tradePartners{QStringLiteral("NightFox"), QStringLiteral("pixelboy"), QStringLiteral("lucky.exe"), QStringLiteral("rush_b_no_stop"), QStringLiteral("moonwalker")};
		for (const QString& partner : tradePartners) {
			const QString confirmationId = world.ConfirmationIdOf(partner);
			const QList<SteamForge::ConfirmationCard*> cards = dialog.findChildren<SteamForge::ConfirmationCard*>();
			const auto card = std::ranges::find_if(cards, [&confirmationId](const SteamForge::ConfirmationCard* shownCard) {
				return shownCard->Confirmation().id == confirmationId;
			});
			QVERIFY(card != cards.end());
			SteamForge::ConfirmationCard* tradeCard = *card;
			Q_EMIT tradeCard->TradeDetailsRequested(confirmationId);
			QTest::qWait(3000);
			const QList<SteamForge::TradeOfferDialog*> tradeWindows = dialog.findChildren<SteamForge::TradeOfferDialog*>();
			QVERIFY(!tradeWindows.isEmpty());
			QVERIFY(tradeWindows.back()->grab().save(QDir(renderDirectory).filePath(QStringLiteral("trade_%1.png").arg(partner.toLower()))));
			tradeWindows.back()->close();
			QTest::qWait(100);
		}
	}
};

QTEST_MAIN(ConfirmationsDemoTest)

#include "confirmations_demo_test.moc"
