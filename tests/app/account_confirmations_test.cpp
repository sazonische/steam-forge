#include "app/account_confirmations.h"
#include "app/account_library.h"
#include "app/confirmation_monitor.h"
#include "steam/steam_clock.h"
#include "support/fake_community_transport.h"
#include "support/fake_web_api_transport.h"

namespace {
constexpr quint64 RECORD_STEAM_ID = 76561198000000001;
constexpr quint64 OTHER_STEAM_ID = 76561198000000002;
constexpr qint64 FAR_FUTURE_SECONDS = 4102444800;
constexpr qint64 PAST_SECONDS = 1000000000;
constexpr QLatin1StringView LIST_PATH{"mobileconf/getlist"};
constexpr QLatin1StringView SINGLE_RESPONSE_PATH{"mobileconf/ajaxop"};
constexpr QLatin1StringView BATCH_RESPONSE_PATH{"mobileconf/multiajaxop"};
constexpr QLatin1StringView ACCESS_TOKEN_METHOD{"GenerateAccessTokenForApp"};
constexpr QLatin1StringView TRADE_OFFER_METHOD{"GetTradeOffer"};
constexpr QLatin1StringView ELIGIBILITY_CHECK_PATH{"market/eligibilitycheck/"};
constexpr QLatin1StringView MARKET_PATH{"market/"};
constexpr qint64 MARKET_ALLOWED_AT_SECONDS = 1893456000;
constexpr QLatin1StringView MARKET_PAGE{R"(<ul class="market_restrictions"><li>Synthetic restriction reason. <a href="https://help.steampowered.com/en/faqs/view/1">Details</a></li></ul>)"};
constexpr int TRADE_TYPE = 2;
constexpr int MARKET_LISTING_TYPE = 3;

struct TestEnvironment {
	QTemporaryDir temporaryDirectory;
	SteamForge::AccountLibrary library{SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions")))};
	FakeWebApiTransport webApiTransport;
	FakeCommunityTransport communityTransport;
	SteamForge::SteamClock steamClock;
};

SteamForge::AuthenticatorRecord Record(quint64 steamId = RECORD_STEAM_ID) {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source.importedAt = record.createdAt;
	record.account.steamId = steamId;
	record.account.accountName = QStringLiteral("synthetic_user");
	record.secrets.sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
	record.secrets.identitySecret = QByteArray::fromBase64("FBMSERAPDg0MCwoJCAcGBQQDAgE=");
	record.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-000000000001");
	return record;
}

SteamForge::AccountSession Session(const SteamForge::AuthenticatorRecord& record, qint64 accessTokenExpiresAt = FAR_FUTURE_SECONDS) {
	return {
		.recordId = record.id,
		.steamId = *record.account.steamId,
		.accountName = record.account.accountName,
		.refreshToken = SyntheticJwt(*record.account.steamId, {QStringLiteral("derive")}, FAR_FUTURE_SECONDS),
		.accessToken = SyntheticJwt(*record.account.steamId, {QStringLiteral("mobile")}, accessTokenExpiresAt),
		.signedInAt = QDateTime::currentDateTimeUtc(),
	};
}

SteamForge::AuthenticatorRecord AddSignedInRecord(TestEnvironment& environment) {
	SteamForge::AuthenticatorRecord record = Record();
	if (!environment.library.Add(record).has_value() || !environment.library.SaveSession(Session(record)).has_value()) {
		qFatal("The test record could not be saved.");
	}
	return record;
}

QJsonObject Entry(const QString& id, bool allowsBatchResponse = true, int type = TRADE_TYPE, const QString& creatorId = {}) {
	return {
		{QStringLiteral("id"), id},
		{QStringLiteral("nonce"), id + QStringLiteral("0")},
		{QStringLiteral("creator_id"), creatorId},
		{QStringLiteral("type"), type},
		{QStringLiteral("type_name"), QStringLiteral("Trade Offer")},
		{QStringLiteral("headline"), QStringLiteral("Partner")},
		{QStringLiteral("multi"), allowsBatchResponse},
	};
}

QJsonObject ListOf(const QJsonArray& entries) {
	return {{QStringLiteral("success"), true}, {QStringLiteral("conf"), entries}};
}

QJsonObject NeedsAuthentication() {
	return {{QStringLiteral("success"), false}, {QStringLiteral("needauth"), true}};
}

// Restores the default locale even when a check fails, so later tests see the language they expect.
class DefaultLocaleGuard final {
public:
	explicit DefaultLocaleGuard(QLocale::Language language) {
		QLocale::setDefault(QLocale(language));
	}

	~DefaultLocaleGuard() {
		QLocale::setDefault(previousLocale);
	}

	Q_DISABLE_COPY_MOVE(DefaultLocaleGuard)

private:
	QLocale previousLocale;
};

QNetworkCookie EligibilityCookie(bool isAllowed) {
	const QJsonObject eligibility{
		{QStringLiteral("allowed"), isAllowed ? 1 : 0},
		{QStringLiteral("allowed_at_time"), isAllowed ? 0 : MARKET_ALLOWED_AT_SECONDS},
	};
	return QNetworkCookie(QByteArray("webTradeEligibility"), QJsonDocument(eligibility).toJson(QJsonDocument::Compact).toPercentEncoding());
}

QJsonObject Success() {
	return {{QStringLiteral("success"), true}};
}

std::unique_ptr<SteamForge::AccountConfirmations> Confirmations(TestEnvironment& environment, const SteamForge::AuthenticatorRecord& record) {
	return std::make_unique<SteamForge::AccountConfirmations>(environment.webApiTransport, environment.communityTransport, environment.library, environment.steamClock, record.id, *SteamForge::ConfirmationKeysOf(record));
}

void LoadOnce(SteamForge::AccountConfirmations& confirmations) {
	std::size_t finishedCount = 0;
	const QMetaObject::Connection connection = QObject::connect(&confirmations, &SteamForge::AccountConfirmations::LoadFinished, [&finishedCount]() {
		++finishedCount;
	});
	confirmations.Refresh();
	QTRY_COMPARE(finishedCount, std::size_t{1});
	QObject::disconnect(connection);
}

QStringList Ids(const std::vector<SteamForge::PendingConfirmation>& confirmations) {
	QStringList ids;
	for (const SteamForge::PendingConfirmation& confirmation : confirmations) {
		ids.append(confirmation.id);
	}
	return ids;
}
} // namespace

class AccountConfirmationsTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void LoadsTheListWithItsOwnAccountAndReportsNewEntriesOnce() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1")), Entry(QStringLiteral("2"))}));
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("2")), Entry(QStringLiteral("3"))}));
		const auto confirmations = Confirmations(environment, record);
		std::vector<QStringList> arrivals;
		connect(confirmations.get(), &SteamForge::AccountConfirmations::ConfirmationsArrived, this, [&arrivals](const std::vector<SteamForge::PendingConfirmation>& newConfirmations) {
			arrivals.push_back(Ids(newConfirmations));
		});

		LoadOnce(*confirmations);
		QCOMPARE(Ids(confirmations->Confirmations()), QStringList({QStringLiteral("1"), QStringLiteral("2")}));
		const SteamForge::CommunityRequest request = environment.communityTransport.SentTo(LIST_PATH).front();
		QCOMPARE(request.session.steamId, RECORD_STEAM_ID);
		QCOMPARE(request.session.accessToken, environment.library.FindSession(record.id)->accessToken);
		QCOMPARE(ParameterValue(request.parameters, u"a"), QString::number(RECORD_STEAM_ID));
		QCOMPARE(ParameterValue(request.parameters, u"p"), record.secrets.deviceId);

		LoadOnce(*confirmations);
		QCOMPARE(arrivals.size(), std::size_t{2});
		QCOMPARE(arrivals.front(), QStringList({QStringLiteral("1"), QStringLiteral("2")}));
		QCOMPARE(arrivals.back(), QStringList({QStringLiteral("3")}));
	}

	void RenewsTheAccessTokenOnceWhenSteamCommunityRejectsIt() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		const QString renewedAccessToken = SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS - 1);
		SteamProtocol::GenerateAccessTokenForAppResponse tokens;
		tokens.set_access_token(renewedAccessToken.toStdString());
		environment.webApiTransport.AddMessage(ACCESS_TOKEN_METHOD, tokens);
		environment.communityTransport.AddJson(LIST_PATH, NeedsAuthentication());
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"))}));
		const auto confirmations = Confirmations(environment, record);

		LoadOnce(*confirmations);
		QVERIFY(!confirmations->LoadFailure().has_value());
		QCOMPARE(confirmations->Confirmations().size(), std::size_t{1});
		QCOMPARE(environment.webApiTransport.SentTo(ACCESS_TOKEN_METHOD).size(), std::size_t{1});
		QCOMPARE(environment.communityTransport.SentTo(LIST_PATH).back().session.accessToken, renewedAccessToken);
		QCOMPARE(environment.library.FindSession(record.id)->accessToken, renewedAccessToken);
	}

	void AsksForASignInWhenSteamKeepsRejectingTheSession() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		SteamProtocol::GenerateAccessTokenForAppResponse tokens;
		tokens.set_access_token(SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS - 1).toStdString());
		environment.webApiTransport.AddMessage(ACCESS_TOKEN_METHOD, tokens);
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"))}));
		environment.communityTransport.AddJson(LIST_PATH, NeedsAuthentication());
		environment.communityTransport.AddJson(LIST_PATH, NeedsAuthentication());
		const auto confirmations = Confirmations(environment, record);
		int signInRequests = 0;
		connect(confirmations.get(), &SteamForge::AccountConfirmations::SignInRequired, this, [&signInRequests]() {
			++signInRequests;
		});

		LoadOnce(*confirmations);
		LoadOnce(*confirmations);
		QVERIFY(confirmations->LoadFailure().has_value());
		QCOMPARE(confirmations->LoadFailure()->kind, SteamForge::ConfirmationsFailureKind::SIGN_IN_REQUIRED);
		QCOMPARE(signInRequests, 1);
		QVERIFY(confirmations->Confirmations().empty());
		QVERIFY(!confirmations->Respond({QStringLiteral("1")}, SteamForge::ConfirmationDecision::ACCEPT));
		QCOMPARE(environment.communityTransport.SentTo(LIST_PATH).size(), std::size_t{3});
	}

	void RequiresASignInWithoutRequestsWhenTheSessionExpired() {
		TestEnvironment environment;
		SteamForge::AuthenticatorRecord record = Record();
		SteamForge::AccountSession expiredSession = Session(record, PAST_SECONDS);
		expiredSession.refreshToken = SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("derive")}, PAST_SECONDS);
		QVERIFY(environment.library.Add(record).has_value());
		QVERIFY(environment.library.SaveSession(expiredSession).has_value());
		const auto confirmations = Confirmations(environment, record);

		LoadOnce(*confirmations);
		QVERIFY(confirmations->LoadFailure().has_value());
		QCOMPARE(confirmations->LoadFailure()->kind, SteamForge::ConfirmationsFailureKind::SIGN_IN_REQUIRED);
		QCOMPARE(environment.communityTransport.SentCount(), std::size_t{0});
		QCOMPARE(environment.webApiTransport.SentCount(), std::size_t{0});
	}

	void RefusesASessionOfAnotherAccount() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		SteamForge::AccountSession foreignSession = Session(Record(OTHER_STEAM_ID));
		foreignSession.recordId = record.id;
		QVERIFY(environment.library.SaveSession(foreignSession).has_value());
		const auto confirmations = Confirmations(environment, record);

		LoadOnce(*confirmations);
		QVERIFY(confirmations->LoadFailure().has_value());
		QCOMPARE(confirmations->LoadFailure()->kind, SteamForge::ConfirmationsFailureKind::SIGN_IN_REQUIRED);
		QCOMPARE(environment.communityTransport.SentCount(), std::size_t{0});
	}

	void AcceptsOneEntryAndLoadsTheListAgain() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"), false), Entry(QStringLiteral("2"))}));
		environment.communityTransport.AddJson(SINGLE_RESPONSE_PATH, Success());
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("2"))}));
		const auto confirmations = Confirmations(environment, record);
		LoadOnce(*confirmations);

		QVERIFY(confirmations->Respond({QStringLiteral("1")}, SteamForge::ConfirmationDecision::ACCEPT));
		QVERIFY(confirmations->IsResponding(QStringLiteral("1")));
		QCOMPARE(confirmations->Activity(), SteamForge::ConfirmationsActivity::RESPONDING);
		QTRY_COMPARE(environment.communityTransport.SentTo(LIST_PATH).size(), std::size_t{2});
		QTRY_COMPARE(confirmations->Activity(), SteamForge::ConfirmationsActivity::IDLE);
		QCOMPARE(Ids(confirmations->Confirmations()), QStringList({QStringLiteral("2")}));
		const SteamForge::CommunityRequest response = environment.communityTransport.SentTo(SINGLE_RESPONSE_PATH).front();
		QCOMPARE(ParameterValue(response.parameters, u"cid"), QStringLiteral("1"));
		QCOMPARE(ParameterValue(response.parameters, u"ck"), QStringLiteral("10"));
		QCOMPARE(ParameterValue(response.parameters, u"tag"), QStringLiteral("accept"));
		QVERIFY(!confirmations->ResponseFailure().has_value());
	}

	void SendsSeveralEntriesTogetherOnlyWhenSteamAllowsIt() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"), false), Entry(QStringLiteral("2")), Entry(QStringLiteral("3"))}));
		environment.communityTransport.AddJson(BATCH_RESPONSE_PATH, Success());
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"), false)}));
		const auto confirmations = Confirmations(environment, record);
		LoadOnce(*confirmations);

		QVERIFY(!confirmations->Respond({QStringLiteral("1"), QStringLiteral("2")}, SteamForge::ConfirmationDecision::REJECT));
		QVERIFY(!confirmations->Respond({QStringLiteral("9")}, SteamForge::ConfirmationDecision::REJECT));
		QVERIFY(environment.communityTransport.SentTo(SINGLE_RESPONSE_PATH).empty());
		QVERIFY(environment.communityTransport.SentTo(BATCH_RESPONSE_PATH).empty());

		QVERIFY(confirmations->Respond({QStringLiteral("2"), QStringLiteral("3")}, SteamForge::ConfirmationDecision::REJECT));
		QTRY_COMPARE(environment.communityTransport.SentTo(LIST_PATH).size(), std::size_t{2});
		QTRY_COMPARE(confirmations->Activity(), SteamForge::ConfirmationsActivity::IDLE);
		const SteamForge::CommunityRequest response = environment.communityTransport.SentTo(BATCH_RESPONSE_PATH).front();
		QCOMPARE(ParameterValues(response.parameters, u"cid[]"), QStringList({QStringLiteral("2"), QStringLiteral("3")}));
		QCOMPARE(ParameterValue(response.parameters, u"tag"), QStringLiteral("reject"));
	}

	void ALostAnswerIsCheckedByReloadingNotBySendingAgain() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"))}));
		environment.communityTransport.AddFailure(SINGLE_RESPONSE_PATH, SteamForge::WebApiErrorKind::TIMEOUT);
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"))}));
		const auto confirmations = Confirmations(environment, record);
		LoadOnce(*confirmations);

		QVERIFY(confirmations->Respond({QStringLiteral("1")}, SteamForge::ConfirmationDecision::ACCEPT));
		QTRY_COMPARE(environment.communityTransport.SentTo(LIST_PATH).size(), std::size_t{2});
		QTRY_COMPARE(confirmations->Activity(), SteamForge::ConfirmationsActivity::IDLE);
		QCOMPARE(environment.communityTransport.SentTo(SINGLE_RESPONSE_PATH).size(), std::size_t{1});
		QVERIFY(confirmations->ResponseFailure().has_value());
		QCOMPARE(confirmations->ResponseFailure()->kind, SteamForge::ConfirmationsFailureKind::CONNECTION);
		QCOMPARE(Ids(confirmations->Confirmations()), QStringList({QStringLiteral("1")}));
	}

	void SignsEveryDecisionWithANewTime() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1")), Entry(QStringLiteral("2"))}));
		environment.communityTransport.AddJson(SINGLE_RESPONSE_PATH, Success());
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("2"))}));
		environment.communityTransport.AddJson(SINGLE_RESPONSE_PATH, Success());
		environment.communityTransport.AddJson(LIST_PATH, ListOf({}));
		const auto confirmations = Confirmations(environment, record);
		LoadOnce(*confirmations);

		QVERIFY(confirmations->Respond({QStringLiteral("1")}, SteamForge::ConfirmationDecision::ACCEPT));
		QTRY_COMPARE(environment.communityTransport.SentTo(LIST_PATH).size(), std::size_t{2});
		QTRY_COMPARE(confirmations->Activity(), SteamForge::ConfirmationsActivity::IDLE);
		QVERIFY(confirmations->Respond({QStringLiteral("2")}, SteamForge::ConfirmationDecision::ACCEPT));
		QTRY_COMPARE(environment.communityTransport.SentTo(LIST_PATH).size(), std::size_t{3});
		QTRY_COMPARE(confirmations->Activity(), SteamForge::ConfirmationsActivity::IDLE);

		const std::vector<SteamForge::CommunityRequest> responses = environment.communityTransport.SentTo(SINGLE_RESPONSE_PATH);
		QVERIFY(ParameterValue(responses.front().parameters, u"t").toLongLong() < ParameterValue(responses.back().parameters, u"t").toLongLong());
	}

	void LoadsTheItemsOfTradeConfirmationsOnly() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"), true, TRADE_TYPE, QStringLiteral("6100")), Entry(QStringLiteral("2"), true, MARKET_LISTING_TYPE, QStringLiteral("7200"))}));
		const QJsonObject offer{{QStringLiteral("tradeofferid"), QStringLiteral("6100")}, {QStringLiteral("items_to_give"), QJsonArray{}}, {QStringLiteral("items_to_receive"), QJsonArray{}}};
		environment.webApiTransport.AddResponse(TRADE_OFFER_METHOD, QJsonDocument(QJsonObject{{QStringLiteral("response"), QJsonObject{{QStringLiteral("offer"), offer}}}}).toJson());
		const auto confirmations = Confirmations(environment, record);

		LoadOnce(*confirmations);
		QTRY_VERIFY(confirmations->FindTradeOffer(QStringLiteral("6100")) != nullptr);
		QVERIFY(confirmations->FindTradeOffer(QStringLiteral("7200")) == nullptr);
		const std::vector<FakeWebApiTransport::SentRequest> offerRequests = environment.webApiTransport.SentTo(TRADE_OFFER_METHOD);
		QCOMPARE(offerRequests.size(), std::size_t{1});
		QCOMPARE(offerRequests.front().accessToken, environment.library.FindSession(record.id)->accessToken);
	}

	void LoadsTheMarketRestrictionOnceWithItsReasons() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf(QJsonArray()));
		environment.communityTransport.AddJson(LIST_PATH, ListOf(QJsonArray()));
		environment.communityTransport.AddCookies(ELIGIBILITY_CHECK_PATH, QList<QNetworkCookie>{EligibilityCookie(false)});
		environment.communityTransport.AddResponse(MARKET_PATH, QString(MARKET_PAGE).toUtf8());
		const auto confirmations = Confirmations(environment, record);

		LoadOnce(*confirmations);
		QTRY_VERIFY(confirmations->ActiveMarketRestriction().has_value() && !confirmations->ActiveMarketRestriction()->reasons.empty());
		LoadOnce(*confirmations);
		const SteamForge::MarketRestriction& restriction = *confirmations->ActiveMarketRestriction();
		QCOMPARE(restriction.allowedAt, QDateTime::fromSecsSinceEpoch(MARKET_ALLOWED_AT_SECONDS, QTimeZone::UTC));
		QCOMPARE(restriction.reasons.front().text, QStringLiteral("Synthetic restriction reason."));
		QCOMPARE(environment.communityTransport.SentTo(ELIGIBILITY_CHECK_PATH).size(), std::size_t{1});
		const std::vector<SteamForge::CommunityRequest> pageRequests = environment.communityTransport.SentTo(MARKET_PATH);
		QCOMPARE(pageRequests.size(), std::size_t{1});
		QCOMPARE(pageRequests.front().session.steamId, RECORD_STEAM_ID);
	}

	void LoadsTheReasonsAgainInANewInterfaceLanguage() {
		const DefaultLocaleGuard localeGuard(QLocale::English);
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf(QJsonArray()));
		for (int i = 0; i < 2; ++i) {
			environment.communityTransport.AddCookies(ELIGIBILITY_CHECK_PATH, QList<QNetworkCookie>{EligibilityCookie(false)});
			environment.communityTransport.AddResponse(MARKET_PATH, QString(MARKET_PAGE).toUtf8());
		}
		const auto confirmations = Confirmations(environment, record);

		LoadOnce(*confirmations);
		QTRY_VERIFY(confirmations->ActiveMarketRestriction().has_value() && !confirmations->ActiveMarketRestriction()->reasons.empty());
		QCOMPARE(confirmations->ActiveMarketRestriction()->reasonsLanguage, QStringLiteral("english"));
		QLocale::setDefault(QLocale(QLocale::Russian));
		confirmations->RecheckMarketRestriction();
		QTRY_COMPARE(confirmations->ActiveMarketRestriction()->reasonsLanguage, QStringLiteral("russian"));
		QCOMPARE(environment.communityTransport.SentTo(MARKET_PATH).size(), std::size_t{2});
	}

	void ShowsNoRestrictionForAnAllowedAccount() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		environment.communityTransport.AddJson(LIST_PATH, ListOf(QJsonArray()));
		environment.communityTransport.AddCookies(ELIGIBILITY_CHECK_PATH, QList<QNetworkCookie>{EligibilityCookie(true)});
		const auto confirmations = Confirmations(environment, record);

		LoadOnce(*confirmations);
		QTRY_COMPARE(environment.communityTransport.SentTo(ELIGIBILITY_CHECK_PATH).size(), std::size_t{1});
		QTest::qWait(10);
		QVERIFY(!confirmations->ActiveMarketRestriction().has_value());
		QVERIFY(environment.communityTransport.SentTo(MARKET_PATH).empty());
	}

	void StopsAskingForATradeOfferSteamDoesNotReturn() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord record = AddSignedInRecord(environment);
		for (int i = 0; i < 3; ++i) {
			environment.communityTransport.AddJson(LIST_PATH, ListOf({Entry(QStringLiteral("1"), true, TRADE_TYPE, QStringLiteral("6100"))}));
		}
		const auto confirmations = Confirmations(environment, record);

		for (int i = 0; i < 3; ++i) {
			LoadOnce(*confirmations);
			QTest::qWait(10);
		}
		QCOMPARE(environment.webApiTransport.SentTo(TRADE_OFFER_METHOD).size(), std::size_t{2});
		QVERIFY(confirmations->FindTradeOffer(QStringLiteral("6100")) == nullptr);
		QCOMPARE(confirmations->Confirmations().size(), std::size_t{1});
	}

	void MonitorsOnlySignedInAccountsThatCanSign() {
		TestEnvironment environment;
		const SteamForge::AuthenticatorRecord signedInRecord = AddSignedInRecord(environment);
		SteamForge::AuthenticatorRecord codesOnlyRecord = Record(OTHER_STEAM_ID);
		codesOnlyRecord.secrets.identitySecret.clear();
		QVERIFY(environment.library.Add(codesOnlyRecord).has_value());
		QVERIFY(environment.library.SaveSession(Session(codesOnlyRecord)).has_value());

		SteamForge::ConfirmationMonitor monitor(environment.webApiTransport, environment.communityTransport, environment.library, environment.steamClock);
		int accountChanges = 0;
		connect(&monitor, &SteamForge::ConfirmationMonitor::AccountsChanged, this, [&accountChanges]() {
			++accountChanges;
		});
		monitor.Start();
		QVERIFY(monitor.Find(signedInRecord.id) != nullptr);
		QCOMPARE(monitor.Find(signedInRecord.id)->Keys().steamId, RECORD_STEAM_ID);
		QVERIFY(monitor.Find(codesOnlyRecord.id) == nullptr);
		QCOMPARE(accountChanges, 1);

		const QPointer<SteamForge::AccountConfirmations> firstConfirmations = monitor.Find(signedInRecord.id);
		SteamForge::AuthenticatorRecord changedRecord = signedInRecord;
		changedRecord.secrets.identitySecret = QByteArray::fromBase64("AQEBAQEBAQEBAQEBAQEBAQEBAQE=");
		QVERIFY(environment.library.Save(changedRecord).has_value());
		QVERIFY(monitor.Find(signedInRecord.id) != nullptr);
		QVERIFY(monitor.Find(signedInRecord.id) != firstConfirmations.data());
		QCOMPARE(monitor.Find(signedInRecord.id)->Keys().identitySecret, changedRecord.secrets.identitySecret);
		QTRY_VERIFY(firstConfirmations.isNull());

		QVERIFY(environment.library.RemoveSession(signedInRecord.id).has_value());
		QVERIFY(monitor.Find(signedInRecord.id) == nullptr);
		QCOMPARE(accountChanges, 3);
	}
};

QTEST_GUILESS_MAIN(AccountConfirmationsTest)

#include "account_confirmations_test.moc"
