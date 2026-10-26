#include "app/account_library.h"
#include "app/steam_login.h"
#include "steam/steam_clock.h"
#include "support/fake_web_api_transport.h"

#include "steam_two_factor.pb.h"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

namespace {
constexpr quint64 RECORD_STEAM_ID = 76561198000000001;
constexpr quint64 OTHER_STEAM_ID = 76561198000000002;
constexpr unsigned int TEST_KEY_BITS = 2048;
constexpr int MOBILE_APP_PLATFORM = 3;
constexpr int DEVICE_CODE_TYPE = 3;
constexpr int EMAIL_CODE_TYPE = 2;
constexpr unsigned int MOVE_VERSION = 2;
constexpr qint64 FAR_FUTURE_SECONDS = 4102444800;
constexpr std::string_view WEAK_TOKEN{"synthetic-weak-token"};

constexpr QLatin1StringView RSA_KEY_METHOD{"GetPasswordRSAPublicKey"};
constexpr QLatin1StringView BEGIN_METHOD{"BeginAuthSessionViaCredentials"};
constexpr QLatin1StringView GUARD_CODE_METHOD{"UpdateAuthSessionWithSteamGuardCode"};
constexpr QLatin1StringView POLL_METHOD{"PollAuthSessionStatus"};
constexpr QLatin1StringView ACCESS_TOKEN_METHOD{"GenerateAccessTokenForApp"};
constexpr QLatin1StringView MOVE_START_METHOD{"RemoveAuthenticatorViaChallengeStart"};
constexpr QLatin1StringView MOVE_FINISH_METHOD{"RemoveAuthenticatorViaChallengeContinue"};

QString HexParameter(EVP_PKEY* key, const char* parameterName) {
	BIGNUM* number = nullptr;
	EVP_PKEY_get_bn_param(key, parameterName, &number);
	char* hex = BN_bn2hex(number);
	const QString hexText = QString::fromLatin1(hex);
	OPENSSL_free(hex);
	BN_free(number);
	return hexText;
}

SteamForge::AuthenticatorRecord Record() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source.importedAt = record.createdAt;
	record.account.steamId = RECORD_STEAM_ID;
	record.account.accountName = QStringLiteral("synthetic_user");
	record.secrets.sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
	return record;
}

SteamForge::AuthenticatorRecord NewAccount() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	return record;
}

// Storage and transport of one sign-in, isolated in a temporary folder.
struct LoginEnvironment {
	QTemporaryDir directory;
	SteamForge::AccountLibrary library{SteamForge::AuthenticatorStore(directory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(directory.filePath(QStringLiteral("sessions")))};
	FakeWebApiTransport transport;
	const SteamForge::SteamClock steamClock;
};

void AddRsaKey(FakeWebApiTransport& transport) {
	EVP_PKEY* key = EVP_RSA_gen(TEST_KEY_BITS);
	SteamProtocol::GetPasswordRsaPublicKeyResponse response;
	response.set_publickey_mod(HexParameter(key, OSSL_PKEY_PARAM_RSA_N).toStdString());
	response.set_publickey_exp(HexParameter(key, OSSL_PKEY_PARAM_RSA_E).toStdString());
	response.set_timestamp(1234567);
	EVP_PKEY_free(key);
	transport.AddMessage(RSA_KEY_METHOD, response);
}

void AddSessionStart(FakeWebApiTransport& transport, quint64 steamId, std::initializer_list<int> confirmationTypes, const std::string& message = {}) {
	SteamProtocol::BeginAuthSessionViaCredentialsResponse response;
	response.set_client_id(42);
	response.set_request_id("request");
	response.set_interval(0.01F);
	response.set_steamid(steamId);
	response.set_weak_token(std::string(WEAK_TOKEN));
	for (const int confirmationType : confirmationTypes) {
		SteamProtocol::AllowedConfirmation* confirmation = response.add_allowed_confirmations();
		confirmation->set_confirmation_type(confirmationType);
		confirmation->set_associated_message(message);
	}
	transport.AddMessage(BEGIN_METHOD, response);
}

void AddTokens(FakeWebApiTransport& transport, quint64 steamId, bool includesAccessToken = true) {
	SteamProtocol::PollAuthSessionStatusResponse response;
	response.set_refresh_token(SyntheticJwt(steamId, {QStringLiteral("derive"), QStringLiteral("mobile")}, FAR_FUTURE_SECONDS).toStdString());
	if (includesAccessToken) {
		response.set_access_token(SyntheticJwt(steamId, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS).toStdString());
	}
	response.set_account_name("synthetic_user");
	transport.AddMessage(POLL_METHOD, response);
}

void AddMovedAuthenticator(FakeWebApiTransport& transport) {
	SteamProtocol::RemoveAuthenticatorViaChallengeContinueResponse response;
	response.set_success(true);
	SteamProtocol::ReplacementToken* replacement = response.mutable_replacement_token();
	replacement->set_shared_secret(QByteArray::fromHex("1111111111111111111111111111111111111111").toStdString());
	replacement->set_identity_secret(QByteArray::fromHex("2222222222222222222222222222222222222222").toStdString());
	replacement->set_revocation_code("R54321");
	replacement->set_account_name("synthetic_user");
	replacement->set_steamid(RECORD_STEAM_ID);
	transport.AddMessage(MOVE_FINISH_METHOD, response);
}

void AddMoveStart(FakeWebApiTransport& transport) {
	SteamProtocol::RemoveAuthenticatorViaChallengeStartResponse response;
	response.set_success(true);
	transport.AddMessage(MOVE_START_METHOD, response);
}
} // namespace

class SteamLoginTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ARecordWithoutSteamIdLearnsItWhenSteamAcceptsItsCode() {
		LoginEnvironment environment;
		SteamForge::AuthenticatorRecord record = Record();
		record.account.steamId.reset();
		record.account.accountName.clear();
		record.label = QStringLiteral("otpauth entry");
		QVERIFY(environment.library.Add(record).has_value());
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddResponse(GUARD_CODE_METHOD, QByteArray());
		AddTokens(environment.transport, RECORD_STEAM_ID);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, record);
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("synthetic password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::SUCCEEDED);

		const SteamForge::AuthenticatorRecord* storedRecord = environment.library.FindRecord(record.id);
		QVERIFY(storedRecord != nullptr);
		QCOMPARE(storedRecord->account.steamId, std::optional<quint64>(RECORD_STEAM_ID));
		QCOMPARE(storedRecord->account.accountName, QStringLiteral("synthetic_user"));
		QCOMPARE(storedRecord->label, QStringLiteral("otpauth entry"));
		QCOMPARE(storedRecord->secrets.sharedSecret, record.secrets.sharedSecret);
		QCOMPARE(environment.library.Records().size(), std::size_t{1});
	}

	void ACodeTypedByTheUserDoesNotProveWhoseRecordItIs() {
		LoginEnvironment environment;
		SteamForge::AuthenticatorRecord record = Record();
		record.account.steamId.reset();
		record.secrets.sharedSecret.clear();
		QVERIFY(environment.library.Add(record).has_value());
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddResponse(GUARD_CODE_METHOD, QByteArray());
		AddTokens(environment.transport, RECORD_STEAM_ID);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, record);
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("synthetic password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		login.SubmitCode(QStringLiteral("ABCDE"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::SUCCEEDED);

		QVERIFY(!environment.library.FindRecord(record.id)->account.steamId.has_value());
		QCOMPARE(environment.library.FindSession(record.id)->steamId, RECORD_STEAM_ID);
	}

	void SubmitsItsOwnGuardCodeAndSignsIn() {
		LoginEnvironment environment;
		const SteamForge::AuthenticatorRecord record = Record();
		QVERIFY(environment.library.Add(record).has_value());
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddResponse(GUARD_CODE_METHOD, QByteArray());
		AddTokens(environment.transport, RECORD_STEAM_ID);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, record);
		const QSignalSpy successSpy(&login, &SteamForge::SteamLogin::Succeeded);
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("synthetic password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::SUCCEEDED);

		QCOMPARE(successSpy.count(), 1);
		const auto session = successSpy.at(0).at(0).value<SteamForge::AccountSession>();
		QCOMPARE(session.recordId, record.id);
		QCOMPARE(session.steamId, RECORD_STEAM_ID);
		QCOMPARE(session.accountName, QStringLiteral("synthetic_user"));
		QVERIFY(environment.library.FindSession(record.id).has_value());
		QCOMPARE(environment.library.Records().size(), std::size_t{1});

		SteamProtocol::BeginAuthSessionViaCredentialsRequest beginRequest;
		QVERIFY(beginRequest.ParseFromString(environment.transport.SentTo(BEGIN_METHOD).front().protobufBody.toStdString()));
		QCOMPARE(beginRequest.platform_type(), MOBILE_APP_PLATFORM);
		QCOMPARE(beginRequest.device_details().platform_type(), MOBILE_APP_PLATFORM);
		QCOMPARE(beginRequest.encryption_timestamp(), quint64{1234567});
		QVERIFY(!QByteArray::fromStdString(beginRequest.encrypted_password()).contains("synthetic password"));

		SteamProtocol::UpdateAuthSessionWithSteamGuardCodeRequest codeRequest;
		QVERIFY(codeRequest.ParseFromString(environment.transport.SentTo(GUARD_CODE_METHOD).front().protobufBody.toStdString()));
		QCOMPARE(codeRequest.code_type(), DEVICE_CODE_TYPE);
		QCOMPARE(codeRequest.code().size(), std::size_t{5});
		QCOMPARE(codeRequest.steamid(), RECORD_STEAM_ID);
	}

	void StopsWhenTheAccountIsNotTheRecordAccount() {
		LoginEnvironment environment;
		const SteamForge::AuthenticatorRecord record = Record();
		QVERIFY(environment.library.Add(record).has_value());
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, OTHER_STEAM_ID, {DEVICE_CODE_TYPE});

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, record);
		login.Start(QStringLiteral("someone_else"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::FAILED);
		QCOMPARE(login.Failure()->kind, SteamForge::LoginFailureKind::WRONG_ACCOUNT);
		QVERIFY(environment.transport.SentTo(GUARD_CODE_METHOD).empty());
		QVERIFY(environment.transport.SentTo(POLL_METHOD).empty());
	}

	void ReportsAWrongPassword() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		environment.transport.AddSteamError(BEGIN_METHOD, SteamForge::SteamResult::INVALID_PASSWORD);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, Record());
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("wrong"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::FAILED);
		QCOMPARE(login.Failure()->kind, SteamForge::LoginFailureKind::WRONG_CREDENTIALS);
	}

	void NewAccountSignsInWithTheEmailCode() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {EMAIL_CODE_TYPE}, "example.com");
		environment.transport.AddResponse(GUARD_CODE_METHOD, QByteArray());
		AddTokens(environment.transport, RECORD_STEAM_ID);

		const SteamForge::AuthenticatorRecord newAccount = NewAccount();
		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, newAccount);
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_EMAIL_CODE);
		QCOMPARE(login.EmailDomain(), QStringLiteral("example.com"));
		QVERIFY(environment.library.Records().empty());

		login.SubmitCode(QStringLiteral(" 4b7Q2 "));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::SUCCEEDED);
		SteamProtocol::UpdateAuthSessionWithSteamGuardCodeRequest codeRequest;
		QVERIFY(codeRequest.ParseFromString(environment.transport.SentTo(GUARD_CODE_METHOD).front().protobufBody.toStdString()));
		QCOMPARE(codeRequest.code_type(), EMAIL_CODE_TYPE);
		QCOMPARE(QString::fromStdString(codeRequest.code()), QStringLiteral("4b7Q2"));

		const SteamForge::AuthenticatorRecord* added = environment.library.FindRecord(newAccount.id);
		QVERIFY(added != nullptr);
		QCOMPARE(added->account.steamId, std::optional(RECORD_STEAM_ID));
		QCOMPARE(added->account.accountName, QStringLiteral("synthetic_user"));
		QCOMPARE(added->source.origin, SteamForge::RecordOrigin::STEAM_SIGN_IN);
		QVERIFY(added->secrets.sharedSecret.isEmpty());
		QVERIFY(environment.library.FindSession(newAccount.id).has_value());
	}

	void NewAccountSignInUsesTheStoredRecordOfTheAccount() {
		LoginEnvironment environment;
		const SteamForge::AuthenticatorRecord stored = Record();
		QVERIFY(environment.library.Add(stored).has_value());
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddResponse(GUARD_CODE_METHOD, QByteArray());
		AddTokens(environment.transport, RECORD_STEAM_ID);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, NewAccount());
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::SUCCEEDED);
		QCOMPARE(login.Record().id, stored.id);
		QCOMPARE(environment.library.Records().size(), std::size_t{1});
		QVERIFY(environment.library.FindSession(stored.id).has_value());
		QCOMPARE(environment.transport.SentTo(GUARD_CODE_METHOD).size(), std::size_t{1});
	}

	void MovesSteamGuardWithAnSmsCode() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		AddMoveStart(environment.transport);
		AddMovedAuthenticator(environment.transport);

		const SteamForge::AuthenticatorRecord newAccount = NewAccount();
		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, newAccount);
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		QVERIFY(login.CanMoveGuard());

		login.StartGuardMove();
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_MOVE_CODE);
		QCOMPARE(environment.transport.SentTo(MOVE_START_METHOD).front().accessToken, QString::fromStdString(std::string(WEAK_TOKEN)));

		const QSignalSpy movedSpy(&login, &SteamForge::SteamLogin::GuardMoved);
		login.SubmitCode(QStringLiteral(" 12345 "));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::GUARD_MOVED);
		QCOMPARE(movedSpy.count(), 1);
		SteamProtocol::RemoveAuthenticatorViaChallengeContinueRequest moveRequest;
		QVERIFY(moveRequest.ParseFromString(environment.transport.SentTo(MOVE_FINISH_METHOD).front().protobufBody.toStdString()));
		QCOMPARE(QString::fromStdString(moveRequest.sms_code()), QStringLiteral("12345"));
		QVERIFY(moveRequest.generate_new_token());
		QCOMPARE(moveRequest.version(), MOVE_VERSION);
		QCOMPARE(environment.transport.SentTo(MOVE_FINISH_METHOD).front().accessToken, QString::fromStdString(std::string(WEAK_TOKEN)));

		const SteamForge::AuthenticatorRecord* moved = environment.library.FindRecord(newAccount.id);
		QVERIFY(moved != nullptr);
		QCOMPARE(moved->secrets.sharedSecret, QByteArray::fromHex("1111111111111111111111111111111111111111"));
		QCOMPARE(moved->secrets.revocationCode, QStringLiteral("R54321"));
		QVERIFY(moved->secrets.deviceId.startsWith(QStringLiteral("android:")));
		QCOMPARE(moved->source.origin, SteamForge::RecordOrigin::MOVED_TO_STEAMFORGE);
		QVERIFY(SteamForge::DetermineCapabilities(*moved).canSignConfirmations);
		QVERIFY(login.MovedRecord().has_value());
		// The sign-in started before the move is not continued: a new sign-in uses the moved Steam Guard.
		QVERIFY(environment.transport.SentTo(GUARD_CODE_METHOD).empty());
		QVERIFY(!environment.library.FindSession(newAccount.id).has_value());
	}

	void MoveKeepsTheSecretsOfTheStoredRecord() {
		LoginEnvironment environment;
		const SteamForge::AuthenticatorRecord stored = Record();
		QVERIFY(environment.library.Add(stored).has_value());
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddSteamError(GUARD_CODE_METHOD, SteamForge::SteamResult::TWO_FACTOR_CODE_MISMATCH);
		AddMoveStart(environment.transport);
		AddMovedAuthenticator(environment.transport);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, stored);
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		login.StartGuardMove();
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_MOVE_CODE);
		login.SubmitCode(QStringLiteral("12345"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::GUARD_MOVED);

		QCOMPARE(environment.library.Records().size(), std::size_t{2});
		QCOMPARE(environment.library.FindRecord(stored.id)->secrets.sharedSecret, stored.secrets.sharedSecret);
		QVERIFY(login.Record().id != stored.id);
		QVERIFY(environment.library.FindRecord(login.Record().id) != nullptr);
	}

	void RejectedSmsCodeKeepsWaitingForIt() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		AddMoveStart(environment.transport);
		environment.transport.AddSteamError(MOVE_FINISH_METHOD, SteamForge::SteamResult::SMS_CODE_FAILED);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, NewAccount());
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		login.StartGuardMove();
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_MOVE_CODE);
		login.SubmitCode(QStringLiteral("00000"));
		QTRY_VERIFY(login.IsCodeRejected());
		QCOMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_MOVE_CODE);
		QVERIFY(environment.library.Records().empty());
		QVERIFY(environment.transport.SentTo(GUARD_CODE_METHOD).empty());
	}

	void SignInAfterTheMoveUsesTheMovedSteamGuard() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		AddMoveStart(environment.transport);
		AddMovedAuthenticator(environment.transport);
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddResponse(GUARD_CODE_METHOD, QByteArray());
		AddTokens(environment.transport, RECORD_STEAM_ID);

		SteamForge::SteamLogin moveLogin(environment.transport, environment.steamClock, environment.library, NewAccount());
		moveLogin.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(moveLogin.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		moveLogin.StartGuardMove();
		QTRY_COMPARE(moveLogin.Stage(), SteamForge::LoginStage::AWAITING_MOVE_CODE);
		moveLogin.SubmitCode(QStringLiteral("12345"));
		QTRY_COMPARE(moveLogin.Stage(), SteamForge::LoginStage::GUARD_MOVED);

		SteamForge::SteamLogin signIn(environment.transport, environment.steamClock, environment.library, *moveLogin.MovedRecord());
		signIn.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(signIn.Stage(), SteamForge::LoginStage::SUCCEEDED);
		QCOMPARE(environment.transport.SentTo(GUARD_CODE_METHOD).size(), std::size_t{1});
		QVERIFY(environment.library.FindSession(moveLogin.MovedRecord()->id).has_value());
	}

	void MoveWaitsForTheSmsCodeWhenTheBodyHasNoSuccess() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddMessage(MOVE_START_METHOD, SteamProtocol::RemoveAuthenticatorViaChallengeStartResponse());

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, NewAccount());
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		login.StartGuardMove();
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_MOVE_CODE);
		QVERIFY(!login.MoveError().has_value());
	}

	void RefusedMoveKeepsTheSignInGoing() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddSteamError(MOVE_START_METHOD, SteamForge::SteamResult::INVALID_STATE);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, NewAccount());
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		login.StartGuardMove();
		QTRY_VERIFY(login.MoveError().has_value());
		QCOMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		QCOMPARE(login.MoveError()->steamResult, SteamForge::SteamResult::INVALID_STATE);
	}

	void RejectedOwnCodeLetsTheUserEnterOne() {
		LoginEnvironment environment;
		const SteamForge::AuthenticatorRecord record = Record();
		QVERIFY(environment.library.Add(record).has_value());
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddSteamError(GUARD_CODE_METHOD, SteamForge::SteamResult::TWO_FACTOR_CODE_MISMATCH);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, record);
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::AWAITING_DEVICE_CODE);
		QVERIFY(login.IsCodeRejected());
	}

	void RequestsAnAccessTokenWhenSteamOmitsIt() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddResponse(GUARD_CODE_METHOD, QByteArray());
		AddTokens(environment.transport, RECORD_STEAM_ID, false);
		SteamProtocol::GenerateAccessTokenForAppResponse accessTokenResponse;
		accessTokenResponse.set_access_token(SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS).toStdString());
		environment.transport.AddMessage(ACCESS_TOKEN_METHOD, accessTokenResponse);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, Record());
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::SUCCEEDED);
		QCOMPARE(environment.transport.SentTo(ACCESS_TOKEN_METHOD).size(), std::size_t{1});
	}

	void RejectsTokensOfAnotherAccount() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});
		environment.transport.AddResponse(GUARD_CODE_METHOD, QByteArray());
		AddTokens(environment.transport, OTHER_STEAM_ID);

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, Record());
		const QSignalSpy successSpy(&login, &SteamForge::SteamLogin::Succeeded);
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		QTRY_COMPARE(login.Stage(), SteamForge::LoginStage::FAILED);
		QCOMPARE(login.Failure()->kind, SteamForge::LoginFailureKind::INVALID_TOKENS);
		QCOMPARE(successSpy.count(), 0);
		QVERIFY(environment.library.Records().empty());
	}

	void CancelledLoginIgnoresLateResponses() {
		LoginEnvironment environment;
		AddRsaKey(environment.transport);
		AddSessionStart(environment.transport, RECORD_STEAM_ID, {DEVICE_CODE_TYPE});

		SteamForge::SteamLogin login(environment.transport, environment.steamClock, environment.library, Record());
		login.Start(QStringLiteral("synthetic_user"), QStringLiteral("password"));
		login.Cancel();
		QTest::qWait(50);
		QCOMPARE(login.Stage(), SteamForge::LoginStage::CANCELLED);
		QVERIFY(environment.transport.SentTo(BEGIN_METHOD).empty());
	}
};

QTEST_GUILESS_MAIN(SteamLoginTest)

#include "steam_login_test.moc"
