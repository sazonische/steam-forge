#include "app/account_library.h"
#include "app/confirmation_monitor.h"
#include "app/interface_translator.h"
#include "platform/window_frame.h"
#include "steam/steam_clock.h"
#include "steam/steam_image_cache.h"
#include "steam/trade_offer_service.h"
#include "support/fake_community_transport.h"
#include "support/synthetic_tokens.h"
#include "ui/confirmation_card.h"
#include "ui/confirmations_dialog.h"
#include "ui/record_presentation.h"
#include "ui/theme.h"
#include "ui/trade_offer_dialog.h"

// A temporary demonstration. Trades and partners are invented; with STEAMFORGE_DEMO_ITEMS the items are real
// CS2 items of the public market and their pictures come from the Steam CDN.
namespace {
constexpr qint64 FAR_FUTURE_SECONDS = 4102444800;
// Account numbers from 4000000000 are not issued yet, so profile and inventory links lead to no real person.
constexpr quint64 DEMO_STEAM_ID = 76561201960265729;
constexpr qint64 DEMO_PARTNER_ACCOUNT_BASE = 4000000000;
constexpr std::chrono::milliseconds DEMO_DELAY{350};
constexpr int ITEM_PICTURE_EDGE = 128;
constexpr int AVATAR_EDGE = 96;
constexpr int LARGE_PICTURE_EDGE = 360;
constexpr qint64 OFFER_LIFETIME_SECONDS = 14LL * 24 * 60 * 60;
constexpr qint64 HOLD_SECONDS = 7LL * 24 * 60 * 60;
// The public example item of the reskins.gg viewer; it shows that item, not the demo name next to it.
constexpr QLatin1StringView RESKINS_EXAMPLE_CODE{"001807209A022805300438DDB8A3F70340D7076214080310B9252D000040C23D"};

struct DemoItem {
	QString name;
	QString type;
	QString kind;
	QString rarity;
	qint64 amount = 1;
	QString inspectCode{};
	QStringList fraudWarnings{};
	QJsonObject steamDescription{};
};

DemoItem SteamItem(const QJsonObject& description, qint64 amount, const QStringList& fraudWarnings) {
	return DemoItem{
		.name = description.value(QStringLiteral("name")).toString(),
		.type = description.value(QStringLiteral("type")).toString(),
		.kind = {},
		.rarity = {},
		.amount = amount,
		.inspectCode = description.value(QStringLiteral("inspect_code")).toString(),
		.fraudWarnings = fraudWarnings,
		.steamDescription = description,
	};
}

const QHash<QString, QString> KIND_NAMES{
	{QStringLiteral("rifle"), QStringLiteral("Винтовка")},
	{QStringLiteral("knife"), QStringLiteral("Нож")},
	{QStringLiteral("case"), QStringLiteral("Контейнер")},
	{QStringLiteral("sticker"), QStringLiteral("Наклейка")},
	{QStringLiteral("gloves"), QStringLiteral("Перчатки")},
	{QStringLiteral("pistol"), QStringLiteral("Пистолет")},
};

const QHash<QString, QString> RARITY_NAMES{
	{QStringLiteral("covert"), QStringLiteral("Тайное")},
	{QStringLiteral("classified"), QStringLiteral("Засекреченное")},
	{QStringLiteral("restricted"), QStringLiteral("Запрещённое")},
	{QStringLiteral("milspec"), QStringLiteral("Армейское качество")},
	{QStringLiteral("industrial"), QStringLiteral("Промышленное качество")},
	{QStringLiteral("base"), QStringLiteral("Базового класса")},
	{QStringLiteral("gold"), QStringLiteral("Экстраординарного типа")},
};

const QHash<QString, QString> RARITY_COLORS{
	{QStringLiteral("covert"), QStringLiteral("eb4b4b")},
	{QStringLiteral("classified"), QStringLiteral("d32ce6")},
	{QStringLiteral("restricted"), QStringLiteral("8847ff")},
	{QStringLiteral("milspec"), QStringLiteral("4b69ff")},
	{QStringLiteral("industrial"), QStringLiteral("5e98d9")},
	{QStringLiteral("base"), QStringLiteral("b0c3d9")},
	{QStringLiteral("gold"), QStringLiteral("e4ae39")},
};

QImage DemoItemPicture(const QString& kind, const QColor& rarity) {
	QImage picture(ITEM_PICTURE_EDGE, ITEM_PICTURE_EDGE, QImage::Format_ARGB32_Premultiplied);
	picture.fill(Qt::transparent);
	QPainter painter(&picture);
	painter.setRenderHint(QPainter::Antialiasing);
	QRadialGradient glow(QPointF(64, 64), 64);
	QColor glowColor = rarity;
	glowColor.setAlpha(110);
	glow.setColorAt(0, glowColor);
	glow.setColorAt(1, Qt::transparent);
	painter.fillRect(picture.rect(), glow);
	QLinearGradient metal(QPointF(0, 30), QPointF(0, 100));
	metal.setColorAt(0, QColor(0xe8, 0xec, 0xf1));
	metal.setColorAt(1, QColor(0x8a, 0x93, 0x9f));
	painter.setBrush(metal);
	painter.setPen(QPen(QColor(0x30, 0x34, 0x3c), 2));
	if (kind == QStringLiteral("rifle")) {
		painter.drawPolygon(QPolygonF({{10, 58}, {92, 52}, {118, 54}, {118, 62}, {92, 64}, {70, 66}, {66, 90}, {56, 90}, {58, 68}, {40, 70}, {30, 84}, {12, 84}, {18, 66}, {10, 66}}));
		painter.setBrush(rarity);
		painter.drawRect(QRectF(40, 56, 34, 6));
	} else if (kind == QStringLiteral("knife")) {
		painter.drawPolygon(QPolygonF({{16, 70}, {96, 30}, {112, 26}, {104, 40}, {40, 78}}));
		painter.setBrush(QColor(0x3a, 0x30, 0x2a));
		painter.drawRoundedRect(QRectF(10, 70, 34, 16), 6, 6);
	} else if (kind == QStringLiteral("case")) {
		painter.setBrush(rarity.darker(160));
		painter.drawRoundedRect(QRectF(22, 40, 84, 56), 8, 8);
		painter.setBrush(metal);
		painter.drawRect(QRectF(22, 58, 84, 6));
		painter.drawRoundedRect(QRectF(54, 50, 20, 22), 3, 3);
	} else if (kind == QStringLiteral("sticker")) {
		painter.setBrush(rarity);
		painter.drawEllipse(QRectF(26, 26, 76, 76));
		painter.setBrush(Qt::white);
		painter.drawPolygon(QPolygonF({{64, 36}, {72, 56}, {94, 58}, {77, 72}, {82, 94}, {64, 82}, {46, 94}, {51, 72}, {34, 58}, {56, 56}}));
	} else if (kind == QStringLiteral("gloves")) {
		painter.setBrush(rarity.darker(130));
		painter.drawRoundedRect(QRectF(34, 40, 60, 54), 18, 18);
		for (int finger = 0; finger < 4; ++finger) {
			painter.drawRoundedRect(QRectF(36 + (finger * 14), 18 + (finger % 2 * 4), 12, 34), 6, 6);
		}
	} else {
		painter.drawEllipse(QRectF(24, 46, 34, 34));
		painter.drawRect(QRectF(54, 58, 54, 10));
		painter.drawRect(QRectF(90, 66, 8, 14));
		painter.drawRect(QRectF(76, 66, 8, 10));
	}
	return picture;
}

QImage DemoAvatar(const QString& name, int hue) {
	QImage avatar(AVATAR_EDGE, AVATAR_EDGE, QImage::Format_RGB32);
	QPainter painter(&avatar);
	painter.setRenderHint(QPainter::Antialiasing);
	QLinearGradient background(QPointF(0, 0), QPointF(AVATAR_EDGE, AVATAR_EDGE));
	background.setColorAt(0, QColor::fromHsv(hue, 150, 200));
	background.setColorAt(1, QColor::fromHsv((hue + 40) % 360, 200, 90));
	painter.fillRect(avatar.rect(), background);
	QFont initialsFont = painter.font();
	initialsFont.setPixelSize(40);
	initialsFont.setBold(true);
	painter.setFont(initialsFont);
	painter.setPen(Qt::white);
	painter.drawText(avatar.rect(), Qt::AlignCenter, name.left(2).toUpper());
	return avatar;
}

class DemoWorld {
public:
	DemoWorld(SteamForge::SteamImageCache& images, const QJsonArray& steamItems) :
		imageCache(images) {
		if (steamItems.isEmpty()) {
			AddSyntheticEntries();
			return;
		}
		for (const QJsonValue& itemValue : steamItems) {
			const QJsonObject item = itemValue.toObject();
			allSteamItems.push_back(item);
			steamItemsByGroup[item.value(QStringLiteral("group")).toString()].push_back(item);
		}
		AddSteamEntries();
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

private:
	[[nodiscard]] DemoItem Steam(const QString& group, std::size_t index, qint64 amount = 1, const QStringList& fraudWarnings = {}) const {
		const auto groupItems = steamItemsByGroup.find(group);
		const std::vector<QJsonObject>& items = groupItems != steamItemsByGroup.end() ? groupItems->second : allSteamItems;
		return SteamItem(items.at(index % items.size()), amount, fraudWarnings);
	}

	[[nodiscard]] std::vector<DemoItem> SteamItems(const QString& group, std::size_t firstIndex, std::size_t count) const {
		std::vector<DemoItem> items;
		for (std::size_t i = 0; i < count; ++i) {
			items.push_back(Steam(group, firstIndex + i));
		}
		return items;
	}

	void AddSteamEntries() {
		const QString knife = QStringLiteral("knife");
		const QString gloves = QStringLiteral("gloves");
		const QString weapon = QStringLiteral("weapon");
		const QString misc = QStringLiteral("misc");
		const QString redline = QStringLiteral("redline");
		AddTrade(QStringLiteral("SkinSmith"), 20, {Steam(redline, 0), Steam(weapon, 0)}, {Steam(gloves, 0)}, {});
		AddTrade(QStringLiteral("MarketMaker_RU"), 140, {Steam(misc, 0, 25)}, {Steam(knife, 0)}, {});
		AddTrade(QStringLiteral("Vanilla"), 260, {}, {Steam(misc, 1)}, {});
		std::vector<DemoItem> gifts = SteamItems(weapon, 1, 8);
		std::ranges::move(SteamItems(misc, 2, 6), std::back_inserter(gifts));
		AddTrade(QStringLiteral("GiftHunter"), 32, gifts, {}, {QStringLiteral("Вы ничего не получаете взамен."), QStringLiteral("Аккаунт партнёра создан менее 30 дней назад.")});
		AddSteamListing(Steam(weapon, 9), 75);
		AddTrade(QStringLiteral("Dobby"), 400, {Steam(weapon, 10), Steam(weapon, 11)}, {Steam(weapon, 12)}, {}, HOLD_SECONDS);
		AddSteamListing(Steam(misc, 8), 600);
		AddSteamListing(Steam(knife, 1), 900);
		std::vector<DemoItem> received = SteamItems(misc, 9, 8);
		received.push_back(Steam(misc, 0, 3));
		AddTrade(QStringLiteral("ArtemK"), 1200, SteamItems(weapon, 13, 3), received, {});
		AddOther(QStringLiteral("Создание ключа Web API"), QStringLiteral("Домен: example.com"), 1500);
		AddTrade(QStringLiteral("Collector2026"), 2000, {Steam(weapon, 16)}, {Steam(misc, 0, 4)}, {});
		AddSteamListing(Steam(weapon, 17), 2400);
		AddTrade(QStringLiteral("NightOwl"), 3000, {Steam(knife, 2, 1, {QStringLiteral("Ярлык с именем: ''Подарок от друга''")})}, {Steam(gloves, 1), Steam(weapon, 18)}, {});
		AddSteamListing(Steam(misc, 18), 3600);
	}

	void AddSteamListing(const DemoItem& item, qint64 ageSeconds) {
		const QString price = item.steamDescription.value(QStringLiteral("price_text")).toString();
		const QUrl iconUrl = SteamForge::EconomyImageUrl(item.steamDescription.value(QStringLiteral("icon_url")).toString());
		AddEntry(3, QStringLiteral("Продажа на Торговой площадке"), item.name, {QStringLiteral("Цена на Торговой площадке сейчас: %1").arg(price)}, {}, iconUrl, ageSeconds, QString::number(++nextOfferId));
	}

	void AddSyntheticEntries() {
		AddTrade(QStringLiteral("SkinSmith"), 20, {{QStringLiteral("AK-47 | Красная линия (После полевых испытаний)"), QStringLiteral("Засекреченное Винтовка"), QStringLiteral("rifle"), QStringLiteral("classified"), 1, RESKINS_EXAMPLE_CODE, {}}}, {{QStringLiteral("Перчатки «Гидра» | Изумрудные (Немного поношенное)"), QStringLiteral("Экстраординарный Перчатки"), QStringLiteral("gloves"), QStringLiteral("gold")}}, {});
		AddTrade(QStringLiteral("MarketMaker_RU"), 140, {{QStringLiteral("Кейс «Революция»"), QStringLiteral("Базового класса Контейнер"), QStringLiteral("case"), QStringLiteral("base"), 25}}, {{QStringLiteral("Нож-бабочка | Градиент (Прямо с завода)"), QStringLiteral("★ Тайное Нож"), QStringLiteral("knife"), QStringLiteral("covert")}}, {});
		AddTrade(QStringLiteral("Vanilla"), 260, {}, {{QStringLiteral("Наклейка | Natus Vincere (Голографическая)"), QStringLiteral("Экзотическое Наклейка"), QStringLiteral("sticker"), QStringLiteral("restricted")}}, {});
		AddTrade(QStringLiteral("GiftHunter"), 32, ManyItems(14), {}, {QStringLiteral("Вы ничего не получаете взамен."), QStringLiteral("Аккаунт партнёра создан менее 30 дней назад.")});
		AddMarketListing(QStringLiteral("M4A4 | Вой (Немного поношенное)"), QStringLiteral("rifle"), QStringLiteral("covert"), QStringLiteral("Цена: 45 210,00 руб. Вы получите 39 313,04 руб."), 75);
		AddTrade(QStringLiteral("Dobby"), 400, {{QStringLiteral("USP-S | Убийство подтверждено"), QStringLiteral("Тайное Пистолет"), QStringLiteral("pistol"), QStringLiteral("covert")}, {QStringLiteral("Glock-18 | Градиент"), QStringLiteral("Запрещённое Пистолет"), QStringLiteral("pistol"), QStringLiteral("restricted")}}, {{QStringLiteral("AWP | Азимов (Закалённое в боях)"), QStringLiteral("Тайное Снайперская винтовка"), QStringLiteral("rifle"), QStringLiteral("covert")}}, {}, HOLD_SECONDS);
		AddMarketListing(QStringLiteral("Кейс «Грёзы и кошмары»"), QStringLiteral("case"), QStringLiteral("base"), QStringLiteral("Цена: 96,50 руб. Вы получите 83,92 руб."), 600);
		AddMarketListing(QStringLiteral("Наклейка | s1mple (Золотая) | Париж 2023"), QStringLiteral("sticker"), QStringLiteral("gold"), QStringLiteral("Цена: 3 120,00 руб. Вы получите 2 713,05 руб."), 900);
		AddTrade(QStringLiteral("ArtemK"), 1200, ManyItems(3), ManyItems(9), {});
		AddOther(QStringLiteral("Создание ключа Web API"), QStringLiteral("Домен: example.com"), 1500);
		AddTrade(QStringLiteral("Collector2026"), 2000, {{QStringLiteral("Desert Eagle | Пламя"), QStringLiteral("Засекреченное Пистолет"), QStringLiteral("pistol"), QStringLiteral("classified")}}, {{QStringLiteral("Кейс «Гидра»"), QStringLiteral("Базового класса Контейнер"), QStringLiteral("case"), QStringLiteral("base"), 4}}, {});
		AddMarketListing(QStringLiteral("AWP | Азимов (Закалённое в боях)"), QStringLiteral("rifle"), QStringLiteral("covert"), QStringLiteral("Цена: 7 450,00 руб. Вы получите 6 478,26 руб."), 2400);
		AddTrade(QStringLiteral("NightOwl"), 3000, {{QStringLiteral("Керамбит | Кровавая паутина"), QStringLiteral("★ Тайное Нож"), QStringLiteral("knife"), QStringLiteral("covert"), 1, {}, {QStringLiteral("Ярлык с именем: ''Подарок от друга''")}}}, {{QStringLiteral("Перчатки «Спорт» | Ледяная буря"), QStringLiteral("Экстраординарный Перчатки"), QStringLiteral("gloves"), QStringLiteral("gold")}, {QStringLiteral("AK-47 | Огненный змей"), QStringLiteral("Тайное Винтовка"), QStringLiteral("rifle"), QStringLiteral("covert")}}, {});
		AddMarketListing(QStringLiteral("Наклейка | Cloud9 (Голографическая)"), QStringLiteral("sticker"), QStringLiteral("restricted"), QStringLiteral("Цена: 512,00 руб. Вы получите 445,22 руб."), 3600);
	}

	std::vector<DemoItem> ManyItems(int count) {
		const std::array kinds{QStringLiteral("case"), QStringLiteral("sticker"), QStringLiteral("pistol"), QStringLiteral("rifle")};
		const std::array rarities{QStringLiteral("base"), QStringLiteral("milspec"), QStringLiteral("industrial"), QStringLiteral("restricted"), QStringLiteral("classified")};
		std::vector<DemoItem> items;
		for (int i = 0; i < count; ++i) {
			const QString& kind = kinds.at(static_cast<std::size_t>(i) % kinds.size());
			const QString& rarity = rarities.at(static_cast<std::size_t>(i) % rarities.size());
			items.push_back({QStringLiteral("Демо-предмет №%1").arg(i + 1), RARITY_NAMES.value(rarity) + QLatin1Char(' ') + KIND_NAMES.value(kind), kind, rarity, 1});
		}
		return items;
	}

	QJsonArray AddItems(const std::vector<DemoItem>& items, QJsonArray& descriptions) {
		QJsonArray assets;
		for (const DemoItem& item : items) {
			if (!item.steamDescription.isEmpty()) {
				assets.append(AddSteamItem(item, descriptions));
				continue;
			}
			const QString classId = QString::number(++nextClassId);
			const QString iconHash = QStringLiteral("demo-%1-%2").arg(item.kind, classId);
			const QString rarityHex = RARITY_COLORS.value(item.rarity);
			const QImage picture = DemoItemPicture(item.kind, QColor::fromString(QLatin1Char('#') + rarityHex));
			imageCache.Insert(SteamForge::EconomyImageUrl(iconHash), picture);
			imageCache.Insert(SteamForge::EconomyImageUrl(iconHash, LARGE_PICTURE_EDGE), picture.scaled(LARGE_PICTURE_EDGE, LARGE_PICTURE_EDGE, Qt::KeepAspectRatio, Qt::SmoothTransformation));
			assets.append(QJsonObject{{QStringLiteral("appid"), 730}, {QStringLiteral("contextid"), QStringLiteral("2")}, {QStringLiteral("assetid"), QString::number(++nextAssetId)}, {QStringLiteral("classid"), classId}, {QStringLiteral("instanceid"), QStringLiteral("0")}, {QStringLiteral("amount"), QString::number(item.amount)}});
			const QString inspectArgument = item.inspectCode.isEmpty() ? QStringLiteral("S%owner_steamid%A%assetid%D%1").arg(classId) : item.inspectCode;
			const QJsonObject inspectAction{
				{QStringLiteral("name"), QStringLiteral("Осмотреть в игре…")},
				{QStringLiteral("link"), QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20") + inspectArgument},
			};
			descriptions.append(QJsonObject{
				{QStringLiteral("appid"), 730},
				{QStringLiteral("classid"), classId},
				{QStringLiteral("instanceid"), QStringLiteral("0")},
				{QStringLiteral("name"), item.name},
				{QStringLiteral("market_hash_name"), item.name},
				{QStringLiteral("type"), item.type},
				{QStringLiteral("name_color"), rarityHex},
				{QStringLiteral("icon_url"), iconHash},
				{QStringLiteral("tradable"), 1},
				{QStringLiteral("marketable"), 1},
				{QStringLiteral("tags"), DemoTags(item)},
				{QStringLiteral("fraudwarnings"), QJsonArray::fromStringList(item.fraudWarnings)},
				{QStringLiteral("actions"), QJsonArray{inspectAction}},
			});
		}
		return assets;
	}

	// Real descriptions of the market search carry no tags and no inspect action; only the exterior in the name is
	// shown, and only the item with the code of the reskins.gg example gets an inspect link.
	QJsonObject AddSteamItem(const DemoItem& item, QJsonArray& descriptions) {
		const QJsonObject& steamDescription = item.steamDescription;
		const QString classId = steamDescription.value(QStringLiteral("classid")).toString();
		QJsonArray actions;
		if (!item.inspectCode.isEmpty()) {
			actions.append(QJsonObject{
				{QStringLiteral("name"), QStringLiteral("Осмотреть в игре…")},
				{QStringLiteral("link"), QStringLiteral("steam://rungame/730/76561202255233023/+csgo_econ_action_preview%20") + item.inspectCode},
			});
		}
		QJsonArray tags;
		const qsizetype exteriorStart = item.name.lastIndexOf(QLatin1Char('('));
		if (exteriorStart > 0 && item.name.endsWith(QLatin1Char(')'))) {
			tags.append(QJsonObject{{QStringLiteral("localized_category_name"), QStringLiteral("Износ")}, {QStringLiteral("localized_tag_name"), item.name.mid(exteriorStart + 1).chopped(1)}});
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

	static QJsonArray DemoTags(const DemoItem& item) {
		QJsonArray tags{
			QJsonObject{{QStringLiteral("localized_category_name"), QStringLiteral("Тип")}, {QStringLiteral("localized_tag_name"), KIND_NAMES.value(item.kind)}},
			QJsonObject{{QStringLiteral("localized_category_name"), QStringLiteral("Качество")}, {QStringLiteral("localized_tag_name"), RARITY_NAMES.value(item.rarity)}, {QStringLiteral("color"), RARITY_COLORS.value(item.rarity)}},
		};
		const qsizetype exteriorStart = item.name.lastIndexOf(QLatin1Char('('));
		if (exteriorStart > 0 && item.name.endsWith(QLatin1Char(')'))) {
			tags.append(QJsonObject{{QStringLiteral("localized_category_name"), QStringLiteral("Износ")}, {QStringLiteral("localized_tag_name"), item.name.mid(exteriorStart + 1).chopped(1)}});
		}
		return tags;
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

	void AddTrade(const QString& partner, qint64 ageSeconds, const std::vector<DemoItem>& give, const std::vector<DemoItem>& receive, const QStringList& warnings, qint64 holdSeconds = 0) {
		const QUrl avatarUrl(QStringLiteral("https://avatars.akamai.steamstatic.com/demo_%1_full.jpg").arg(partner.toLower()));
		imageCache.Insert(avatarUrl, DemoAvatar(partner, static_cast<int>(qHash(partner) % 360)));
		const QString offerId = QString::number(++nextOfferId);
		QJsonArray descriptions;
		const QJsonObject offer{
			{QStringLiteral("tradeofferid"), offerId},
			{QStringLiteral("message"), give.size() > 10 ? QStringLiteral("Забери всё, это подарок :)") : QString()},
			{QStringLiteral("accountid_other"), DEMO_PARTNER_ACCOUNT_BASE + static_cast<qint64>(qHash(partner) % 1000000)},
			{QStringLiteral("is_our_offer"), false},
			{QStringLiteral("expiration_time"), QDateTime::currentSecsSinceEpoch() + OFFER_LIFETIME_SECONDS - ageSeconds},
			{QStringLiteral("escrow_end_date"), holdSeconds > 0 ? QDateTime::currentSecsSinceEpoch() + holdSeconds : 0},
			{QStringLiteral("items_to_give"), AddItems(give, descriptions)},
			{QStringLiteral("items_to_receive"), AddItems(receive, descriptions)},
		};
		offers.insert(offerId, QJsonDocument(QJsonObject{{QStringLiteral("response"), QJsonObject{{QStringLiteral("offer"), offer}, {QStringLiteral("descriptions"), descriptions}}}}).toJson(QJsonDocument::Compact));
		const QStringList summary{QStringLiteral("Вы отдадите предметов: %1").arg(give.size()), QStringLiteral("Вы получите предметов: %1").arg(receive.size())};
		AddEntry(2, QStringLiteral("Предложение обмена"), partner, summary, warnings, avatarUrl, ageSeconds, offerId);
	}

	void AddMarketListing(const QString& name, const QString& kind, const QString& rarity, const QString& price, qint64 ageSeconds) {
		const QUrl iconUrl = SteamForge::EconomyImageUrl(QStringLiteral("demo-listing-%1").arg(++nextClassId));
		imageCache.Insert(iconUrl, DemoItemPicture(kind, QColor::fromString(QLatin1Char('#') + RARITY_COLORS.value(rarity))));
		AddEntry(3, QStringLiteral("Продажа на Торговой площадке"), name, {price}, {}, iconUrl, ageSeconds, QString::number(++nextOfferId));
	}

	void AddOther(const QString& typeName, const QString& summary, qint64 ageSeconds) {
		AddEntry(6, typeName, QStringLiteral("Подтвердите действие с аккаунтом"), {summary}, {QStringLiteral("Подтверждайте, только если начали это действие сами.")}, {}, ageSeconds, {});
	}

	SteamForge::SteamImageCache& imageCache;
	std::vector<QJsonObject> confirmations;
	QHash<QString, QByteArray> offers;
	int nextConfirmationId = 1000;
	int nextOfferId = 6100;
	int nextClassId = 500;
	qint64 nextAssetId = 31000000000;
	std::vector<QJsonObject> allSteamItems;
	std::map<QString, std::vector<QJsonObject>> steamItemsByGroup;
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
		if (renderDirectory.isEmpty() && !isInteractive) {
			QSKIP("Set STEAMFORGE_DEMO or STEAMFORGE_RENDER_DIRECTORY to run the demonstration.");
		}
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);
		SteamForge::InstallWindowFrameStyling(application);
		SteamForge::InterfaceTranslator translator;
		QVERIFY(translator.Apply(SteamForge::InterfaceLanguage::RUSSIAN));

		const QTemporaryDir dataDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(QDir(dataDirectory.path()).filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(QDir(dataDirectory.path()).filePath(QStringLiteral("sessions"))));
		SteamForge::AuthenticatorRecord record;
		record.id = QUuid::createUuid();
		record.account.steamId = DEMO_STEAM_ID;
		record.account.accountName = QStringLiteral("trader_demo");
		record.createdAt = QDateTime::currentDateTimeUtc();
		record.secrets.sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
		record.secrets.identitySecret = QByteArray::fromBase64("FBMSERAPDg0MCwoJCAcGBQQDAgE=");
		record.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-000000000001");
		QVERIFY(library.Add(record).has_value());
		QVERIFY(library.SaveSession({.recordId = record.id, .steamId = DEMO_STEAM_ID, .accountName = QStringLiteral("trader_demo"), .refreshToken = SyntheticJwt(DEMO_STEAM_ID, {QStringLiteral("derive")}, FAR_FUTURE_SECONDS), .accessToken = SyntheticJwt(DEMO_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS), .signedInAt = QDateTime::currentDateTimeUtc()}).has_value());

		QJsonArray steamItems;
		if (const QString steamItemsPath = qEnvironmentVariable("STEAMFORGE_DEMO_ITEMS"); !steamItemsPath.isEmpty()) {
			QFile steamItemsFile(steamItemsPath);
			QVERIFY(steamItemsFile.open(QIODevice::ReadOnly));
			steamItems = QJsonDocument::fromJson(steamItemsFile.readAll()).array();
			QVERIFY(!steamItems.isEmpty());
		}
		SteamForge::SteamImageCache imageCache;
		DemoWorld world(imageCache, steamItems);
		DemoCommunityTransport communityTransport(world);
		DemoWebApiTransport webApiTransport(world);
		SteamForge::SteamClock clock;
		SteamForge::ConfirmationMonitor monitor(webApiTransport, communityTransport, library, clock);
		monitor.Start();

		SteamForge::ConfirmationsDialog dialog(monitor, imageCache, record, SteamForge::RecordDisplayName(record));
		dialog.setWindowTitle(dialog.windowTitle() + (steamItems.isEmpty() ? QStringLiteral(" (демо-данные)") : QStringLiteral(" (демо: настоящие предметы CS2)")));
		dialog.show();
		if (isInteractive) {
			QEventLoop untilClosed;
			connect(&dialog, &QDialog::finished, &untilClosed, &QEventLoop::quit);
			untilClosed.exec();
			return;
		}
		QTest::qWait(steamItems.isEmpty() ? 2500 : 8000);
		auto* scrollArea = dialog.findChild<QScrollArea*>();
		QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("demo_top.png"))));
		scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum() / 5);
		QTest::qWait(100);
		QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("demo_quarter.png"))));
		scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum() / 2);
		QTest::qWait(100);
		QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("demo_middle.png"))));
		scrollArea->verticalScrollBar()->setValue(scrollArea->verticalScrollBar()->maximum());
		QTest::qWait(100);
		QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("demo_bottom.png"))));

		const std::array tradePartners{QStringLiteral("SkinSmith"), QStringLiteral("GiftHunter"), QStringLiteral("Dobby")};
		for (const QString& partner : tradePartners) {
			const QString confirmationId = world.ConfirmationIdOf(partner);
			const QList<SteamForge::ConfirmationCard*> cards = dialog.findChildren<SteamForge::ConfirmationCard*>();
			const auto card = std::ranges::find_if(cards, [&confirmationId](const SteamForge::ConfirmationCard* shownCard) {
				return shownCard->Confirmation().id == confirmationId;
			});
			QVERIFY(card != cards.end());
			SteamForge::ConfirmationCard* tradeCard = *card;
			Q_EMIT tradeCard->TradeDetailsRequested(confirmationId);
			QTest::qWait(steamItems.isEmpty() ? 300 : 3000);
			const QList<SteamForge::TradeOfferDialog*> tradeWindows = dialog.findChildren<SteamForge::TradeOfferDialog*>();
			QVERIFY(!tradeWindows.isEmpty());
			QVERIFY(tradeWindows.back()->grab().save(QDir(renderDirectory).filePath(QStringLiteral("trade_%1.png").arg(partner.toLower()))));
			tradeWindows.back()->resize(tradeWindows.back()->minimumSize());
			QTest::qWait(100);
			QVERIFY(tradeWindows.back()->grab().save(QDir(renderDirectory).filePath(QStringLiteral("trade_%1_small.png").arg(partner.toLower()))));
			tradeWindows.back()->close();
			QTest::qWait(100);
		}
	}
};

QTEST_MAIN(ConfirmationsDemoTest)

#include "confirmations_demo_test.moc"
