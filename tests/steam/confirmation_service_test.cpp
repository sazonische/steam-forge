#include "steam/confirmation_service.h"
#include "support/fake_community_transport.h"

namespace {
// Public synthetic secret (bytes 1..20); expected keys come from node-steam-totp getConfirmationKey.
constexpr QByteArrayView SYNTHETIC_SECRET_BASE64{"AQIDBAUGBwgJCgsMDQ4PEBESExQ="};
constexpr quint64 ACCOUNT_STEAM_ID = 76561198000000001;
constexpr QLatin1StringView DEVICE_ID{"android:00000000-0000-0000-0000-000000000001"};
constexpr QLatin1StringView LIST_PATH{"mobileconf/getlist"};
constexpr QLatin1StringView SINGLE_RESPONSE_PATH{"mobileconf/ajaxop"};
constexpr QLatin1StringView BATCH_RESPONSE_PATH{"mobileconf/multiajaxop"};
constexpr std::chrono::sys_seconds SIGNING_TIME{std::chrono::seconds(1710000000)};
// Above 2^53: such identifiers lose digits as JSON doubles.
constexpr QLatin1StringView LARGE_ID{"9007199254740993"};
constexpr QLatin1StringView LARGE_NONCE{"18446744073709551615"};
constexpr qint64 CREATION_TIME = 1710000123;
constexpr int TRADE_TYPE = 2;
constexpr int UNKNOWN_TYPE = 99;

SteamForge::ConfirmationKeys Keys() {
	return {
		.steamId = ACCOUNT_STEAM_ID,
		.deviceId = DEVICE_ID,
		.identitySecret = QByteArray::fromBase64(SYNTHETIC_SECRET_BASE64.toByteArray()),
	};
}

SteamForge::CommunitySession Session() {
	return {
		.steamId = ACCOUNT_STEAM_ID,
		.accessToken = QStringLiteral("synthetic.access.token"),
		.sessionId = QStringLiteral("0123456789abcdef01234567"),
	};
}

SteamForge::PendingConfirmation Confirmation(const QString& id, const QString& nonce, bool allowsBatchResponse) {
	SteamForge::PendingConfirmation confirmation;
	confirmation.id = id;
	confirmation.nonce = nonce;
	confirmation.allowsBatchResponse = allowsBatchResponse;
	return confirmation;
}

QJsonObject TradeEntry() {
	return {
		{QStringLiteral("type"), TRADE_TYPE},
		{QStringLiteral("type_name"), QStringLiteral("Trade Offer")},
		{QStringLiteral("id"), LARGE_ID},
		{QStringLiteral("creator_id"), QStringLiteral("6123456789")},
		{QStringLiteral("nonce"), LARGE_NONCE},
		{QStringLiteral("creation_time"), CREATION_TIME},
		{QStringLiteral("cancel"), QStringLiteral("Cancel")},
		{QStringLiteral("accept"), QStringLiteral("Accept")},
		{QStringLiteral("icon"), QStringLiteral("https://avatars.example/partner.jpg")},
		{QStringLiteral("multi"), true},
		{QStringLiteral("headline"), QStringLiteral("Trade partner")},
		{QStringLiteral("summary"), QJsonArray{QStringLiteral("You will give 1 item"), QStringLiteral("You will receive 2 items")}},
		{QStringLiteral("warn"), QJsonArray{QStringLiteral("Items will be held")}},
	};
}

QByteArray Json(const QJsonObject& object) {
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

void VerifySignedParameters(const SteamForge::RequestParameters& parameters, QLatin1StringView tag) {
	QCOMPARE(ParameterValue(parameters, u"p"), QString(DEVICE_ID));
	QCOMPARE(ParameterValue(parameters, u"a"), QString::number(ACCOUNT_STEAM_ID));
	QCOMPARE(ParameterValue(parameters, u"t"), QString::number(SIGNING_TIME.time_since_epoch().count()));
	QCOMPARE(ParameterValue(parameters, u"m"), QStringLiteral("react"));
	QCOMPARE(ParameterValue(parameters, u"tag"), QString(tag));
	QCOMPARE(ParameterValue(parameters, u"k"), SteamForge::SignConfirmationRequest(Keys().identitySecret, SIGNING_TIME, tag));
}
} // namespace

class ConfirmationServiceTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void SignsLikeTheSteamMobileApplication_data() {
		QTest::addColumn<qint64>("time");
		QTest::addColumn<QString>("tag");
		QTest::addColumn<QString>("expectedKey");
		QTest::newRow("list at 0") << qint64{0} << QStringLiteral("list") << QStringLiteral("Gj4CHwiK6KJTLfB44dFs45ohqLY=");
		QTest::newRow("accept at 0") << qint64{0} << QStringLiteral("accept") << QStringLiteral("+4kwBOeGN2r32E3EIhKPlP3fy+I=");
		QTest::newRow("reject at 0") << qint64{0} << QStringLiteral("reject") << QStringLiteral("iVH2c0VzQebuKkVuoFLaBJxx4/I=");
		QTest::newRow("list in 2024") << qint64{1710000000} << QStringLiteral("list") << QStringLiteral("TaQOEfOBYfMtnXcYPpkr7SxGGg4=");
		QTest::newRow("accept in 2024") << qint64{1710000000} << QStringLiteral("accept") << QStringLiteral("Qk7IZkrsgbrRlRP3ExXAl8M4Vhw=");
		QTest::newRow("reject in 2024") << qint64{1710000000} << QStringLiteral("reject") << QStringLiteral("gZcyGhD8a2tPVXtXO+g1LwafcIs=");
		QTest::newRow("accept beyond 32 bits") << qint64{4294967297} << QStringLiteral("accept") << QStringLiteral("MzH5tbtToTK8MMcFuAU67CLYZMw=");
	}

	void SignsLikeTheSteamMobileApplication() {
		QFETCH(qint64, time);
		QFETCH(QString, tag);
		QFETCH(QString, expectedKey);
		const QByteArray tagBytes = tag.toLatin1();
		const QString key = SteamForge::SignConfirmationRequest(Keys().identitySecret, std::chrono::sys_seconds(std::chrono::seconds(time)), QLatin1StringView(tagBytes));
		QCOMPARE(key, expectedKey);
	}

	void ReadsEveryFieldAndKeepsLargeIdentifiersExact() {
		const auto list = SteamForge::ParseConfirmationList(Json({
			{QStringLiteral("success"), true},
			{QStringLiteral("conf"), QJsonArray{TradeEntry()}},
		}));
		QVERIFY(list.has_value());
		QCOMPARE(list->size(), std::size_t{1});
		const SteamForge::PendingConfirmation& confirmation = list->front();
		QCOMPARE(confirmation.id, QString(LARGE_ID));
		QCOMPARE(confirmation.nonce, QString(LARGE_NONCE));
		QCOMPARE(confirmation.creatorId, QStringLiteral("6123456789"));
		QCOMPARE(confirmation.category, SteamForge::ConfirmationCategory::TRADE);
		QCOMPARE(confirmation.typeName, QStringLiteral("Trade Offer"));
		QCOMPARE(confirmation.headline, QStringLiteral("Trade partner"));
		QCOMPARE(confirmation.summary, QStringList({QStringLiteral("You will give 1 item"), QStringLiteral("You will receive 2 items")}));
		QCOMPARE(confirmation.warnings, QStringList({QStringLiteral("Items will be held")}));
		QVERIFY(confirmation.allowsBatchResponse);
		QCOMPARE(confirmation.createdAt, QDateTime::fromSecsSinceEpoch(CREATION_TIME, QTimeZone::UTC));
		QCOMPARE(confirmation.iconUrl, QUrl(QStringLiteral("https://avatars.example/partner.jpg")));
	}

	void KeepsAnUnknownTypeUnknown() {
		QJsonObject entry = TradeEntry();
		entry.insert(QStringLiteral("type"), UNKNOWN_TYPE);
		const auto list = SteamForge::ParseConfirmationList(Json({
			{QStringLiteral("success"), true},
			{QStringLiteral("conf"), QJsonArray{entry}},
		}));
		QVERIFY(list.has_value());
		QCOMPARE(list->front().category, SteamForge::ConfirmationCategory::OTHER);
		QCOMPARE(list->front().type, UNKNOWN_TYPE);
	}

	void ReadsAnEmptyList() {
		const auto withoutEntries = SteamForge::ParseConfirmationList(Json({{QStringLiteral("success"), true}}));
		QVERIFY(withoutEntries.has_value());
		QVERIFY(withoutEntries->empty());
		const auto emptyArray = SteamForge::ParseConfirmationList(Json({{QStringLiteral("success"), true}, {QStringLiteral("conf"), QJsonArray{}}}));
		QVERIFY(emptyArray.has_value());
		QVERIFY(emptyArray->empty());
	}

	void ReportsARejectedSignIn() {
		const auto list = SteamForge::ParseConfirmationList(Json({{QStringLiteral("success"), false}, {QStringLiteral("needauth"), true}}));
		QVERIFY(!list.has_value());
		QCOMPARE(list.error().kind, SteamForge::WebApiErrorKind::NEEDS_AUTHENTICATION);
	}

	void ReportsTheMessageOfAFailure() {
		const auto list = SteamForge::ParseConfirmationList(Json({{QStringLiteral("success"), false}, {QStringLiteral("message"), QStringLiteral("Invalid authenticator")}}));
		QVERIFY(!list.has_value());
		QCOMPARE(list.error().kind, SteamForge::WebApiErrorKind::UNSUCCESSFUL_RESPONSE);
		QCOMPARE(list.error().message, QStringLiteral("Invalid authenticator"));
	}

	void RejectsTheWholeListWhenAnEntryCannotBeRead() {
		QJsonObject entryWithoutNonce = TradeEntry();
		entryWithoutNonce.remove(QStringLiteral("nonce"));
		const auto list = SteamForge::ParseConfirmationList(Json({
			{QStringLiteral("success"), true},
			{QStringLiteral("conf"), QJsonArray{TradeEntry(), entryWithoutNonce}},
		}));
		QVERIFY(!list.has_value());
		QCOMPARE(list.error().kind, SteamForge::WebApiErrorKind::MALFORMED_RESPONSE);

		const auto notJson = SteamForge::ParseConfirmationList(QByteArray("<html>Sign in</html>"));
		QVERIFY(!notJson.has_value());
		QCOMPARE(notJson.error().kind, SteamForge::WebApiErrorKind::MALFORMED_RESPONSE);
	}

	void GetsTheListWithASignedListTag() {
		FakeCommunityTransport transport;
		transport.AddJson(LIST_PATH, {{QStringLiteral("success"), true}, {QStringLiteral("conf"), QJsonArray{TradeEntry()}}});
		SteamForge::ConfirmationService service(transport);
		QObject context;
		std::optional<std::expected<std::vector<SteamForge::PendingConfirmation>, SteamForge::WebApiError>> result;
		service.GetList(Session(), Keys(), SIGNING_TIME, context, [&result](std::expected<std::vector<SteamForge::PendingConfirmation>, SteamForge::WebApiError> list) {
			result = std::move(list);
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(result->has_value());

		const SteamForge::CommunityRequest request = transport.SentTo(LIST_PATH).front();
		QCOMPARE(request.httpMethod, SteamForge::WebApiHttpMethod::GET);
		QCOMPARE(request.session.steamId, ACCOUNT_STEAM_ID);
		QCOMPARE(request.session.accessToken, Session().accessToken);
		VerifySignedParameters(request.parameters, QLatin1StringView("list"));
	}

	void AcceptsOneConfirmationWithTheAcceptTagAndAllowOperation() {
		FakeCommunityTransport transport;
		transport.AddJson(SINGLE_RESPONSE_PATH, {{QStringLiteral("success"), true}});
		SteamForge::ConfirmationService service(transport);
		QObject context;
		std::optional<std::expected<void, SteamForge::WebApiError>> result;
		const std::array confirmations{Confirmation(LARGE_ID, LARGE_NONCE, false)};
		service.Respond(Session(), Keys(), SIGNING_TIME, confirmations, SteamForge::ConfirmationDecision::ACCEPT, context, [&result](const std::expected<void, SteamForge::WebApiError>& sent) {
			result = sent;
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(result->has_value());

		const SteamForge::CommunityRequest request = transport.SentTo(SINGLE_RESPONSE_PATH).front();
		QCOMPARE(request.httpMethod, SteamForge::WebApiHttpMethod::GET);
		QCOMPARE(ParameterValue(request.parameters, u"op"), QStringLiteral("allow"));
		QCOMPARE(ParameterValue(request.parameters, u"cid"), QString(LARGE_ID));
		QCOMPARE(ParameterValue(request.parameters, u"ck"), QString(LARGE_NONCE));
		VerifySignedParameters(request.parameters, QLatin1StringView("accept"));
	}

	void RejectsSeveralConfirmationsInOneBatch() {
		FakeCommunityTransport transport;
		transport.AddJson(BATCH_RESPONSE_PATH, {{QStringLiteral("success"), true}});
		SteamForge::ConfirmationService service(transport);
		QObject context;
		std::optional<std::expected<void, SteamForge::WebApiError>> result;
		const std::array confirmations{Confirmation(QStringLiteral("11"), QStringLiteral("21"), true), Confirmation(QStringLiteral("12"), QStringLiteral("22"), true)};
		service.Respond(Session(), Keys(), SIGNING_TIME, confirmations, SteamForge::ConfirmationDecision::REJECT, context, [&result](const std::expected<void, SteamForge::WebApiError>& sent) {
			result = sent;
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(result->has_value());

		const SteamForge::CommunityRequest request = transport.SentTo(BATCH_RESPONSE_PATH).front();
		QCOMPARE(request.httpMethod, SteamForge::WebApiHttpMethod::POST);
		QCOMPARE(ParameterValue(request.parameters, u"op"), QStringLiteral("cancel"));
		QCOMPARE(ParameterValues(request.parameters, u"cid[]"), QStringList({QStringLiteral("11"), QStringLiteral("12")}));
		QCOMPARE(ParameterValues(request.parameters, u"ck[]"), QStringList({QStringLiteral("21"), QStringLiteral("22")}));
		VerifySignedParameters(request.parameters, QLatin1StringView("reject"));
	}

	void ReportsADecisionSteamDidNotCarryOut() {
		FakeCommunityTransport transport;
		transport.AddJson(SINGLE_RESPONSE_PATH, {{QStringLiteral("success"), false}});
		SteamForge::ConfirmationService service(transport);
		QObject context;
		std::optional<std::expected<void, SteamForge::WebApiError>> result;
		const std::array confirmations{Confirmation(QStringLiteral("11"), QStringLiteral("21"), false)};
		service.Respond(Session(), Keys(), SIGNING_TIME, confirmations, SteamForge::ConfirmationDecision::ACCEPT, context, [&result](const std::expected<void, SteamForge::WebApiError>& sent) {
			result = sent;
		});
		QTRY_VERIFY(result.has_value());
		QVERIFY(!result->has_value());
		QCOMPARE(result->error().kind, SteamForge::WebApiErrorKind::UNSUCCESSFUL_RESPONSE);
	}
};

QTEST_GUILESS_MAIN(ConfirmationServiceTest)

#include "confirmation_service_test.moc"
