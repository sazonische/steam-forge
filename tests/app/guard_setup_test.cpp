#include "app/account_library.h"
#include "app/guard_setup.h"
#include "steam/steam_clock.h"
#include "support/fake_web_api_transport.h"

#include "steam_two_factor.pb.h"

namespace {
constexpr quint64 RECORD_STEAM_ID = 76561198000000001;
constexpr qint64 FAR_FUTURE_SECONDS = 4102444800;
constexpr int SMS_CONFIRM_TYPE = 1;
constexpr unsigned int MOBILE_AUTHENTICATOR_TYPE = 1;
constexpr unsigned int TWO_FACTOR_VERSION = 2;

constexpr QLatin1StringView ADD_METHOD{"AddAuthenticator"};
constexpr QLatin1StringView FINALIZE_METHOD{"FinalizeAddAuthenticator"};

constexpr QByteArrayView NEW_SHARED_SECRET_HEX{"3333333333333333333333333333333333333333"};

QByteArray NewSharedSecret() {
	return QByteArray::fromHex(NEW_SHARED_SECRET_HEX.toByteArray());
}

SteamForge::AuthenticatorRecord SignedInRecord() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.account.steamId = RECORD_STEAM_ID;
	record.account.accountName = QStringLiteral("synthetic_user");
	record.source.origin = SteamForge::RecordOrigin::STEAM_SIGN_IN;
	record.source.importedAt = record.createdAt;
	return record;
}

SteamForge::AccountSession Session(const SteamForge::AuthenticatorRecord& record) {
	return {
		.recordId = record.id,
		.steamId = RECORD_STEAM_ID,
		.accountName = QStringLiteral("synthetic_user"),
		.refreshToken = SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("derive")}, FAR_FUTURE_SECONDS),
		.accessToken = SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS),
		.signedInAt = QDateTime::currentDateTimeUtc(),
	};
}

void AddAuthenticatorResponse(FakeWebApiTransport& transport) {
	SteamProtocol::AddAuthenticatorResponse response;
	response.set_shared_secret(NewSharedSecret().toStdString());
	response.set_identity_secret(QByteArray::fromHex("4444444444444444444444444444444444444444").toStdString());
	response.set_revocation_code("R11111");
	response.set_serial_number(1234567890);
	response.set_token_gid("abcdef");
	response.set_account_name("synthetic_user");
	response.set_phone_number_hint("34");
	response.set_confirm_type(SMS_CONFIRM_TYPE);
	transport.AddMessage(ADD_METHOD, response);
}

void AddFinalizeSuccess(FakeWebApiTransport& transport) {
	SteamProtocol::FinalizeAddAuthenticatorResponse response;
	response.set_success(true);
	transport.AddMessage(FINALIZE_METHOD, response);
}

// Storage and transport of one setup, isolated in a temporary folder.
struct SetupEnvironment {
	QTemporaryDir directory;
	SteamForge::AccountLibrary library{SteamForge::AuthenticatorStore(directory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(directory.filePath(QStringLiteral("sessions")))};
	FakeWebApiTransport transport;
	const SteamForge::SteamClock steamClock;
};
} // namespace

class GuardSetupTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void SavesTheSecretsBeforeTheActivationCode() {
		SetupEnvironment environment;
		const SteamForge::AuthenticatorRecord record = SignedInRecord();
		QVERIFY(environment.library.Add(record).has_value());
		AddAuthenticatorResponse(environment.transport);
		AddFinalizeSuccess(environment.transport);

		SteamForge::GuardSetup setup(environment.transport, environment.steamClock, environment.library, record, Session(record));
		setup.Start();
		QTRY_COMPARE(setup.Stage(), SteamForge::GuardSetupStage::AWAITING_ACTIVATION_CODE);
		QVERIFY(environment.transport.SentTo(FINALIZE_METHOD).empty());
		const SteamForge::AuthenticatorRecord* pending = environment.library.FindRecord(record.id);
		QCOMPARE(pending->secrets.sharedSecret, NewSharedSecret());
		QCOMPARE(pending->secrets.revocationCode, QStringLiteral("R11111"));
		QCOMPARE(pending->secrets.serialNumber, QStringLiteral("1234567890"));
		QVERIFY(pending->pendingActivation.has_value());
		QCOMPARE(pending->pendingActivation->phoneNumberHint, QStringLiteral("34"));
		QCOMPARE(pending->source.origin, SteamForge::RecordOrigin::ADDED_IN_STEAMFORGE);
		QVERIFY(!SteamForge::DetermineCapabilities(*pending).canGenerateCodes);

		SteamProtocol::AddAuthenticatorRequest addRequest;
		QVERIFY(addRequest.ParseFromString(environment.transport.SentTo(ADD_METHOD).front().protobufBody.toStdString()));
		QCOMPARE(addRequest.steamid(), RECORD_STEAM_ID);
		QCOMPARE(addRequest.authenticator_type(), MOBILE_AUTHENTICATOR_TYPE);
		QCOMPARE(addRequest.version(), TWO_FACTOR_VERSION);
		QCOMPARE(QString::fromStdString(addRequest.device_identifier()), pending->secrets.deviceId);
		QVERIFY(pending->secrets.deviceId.startsWith(QStringLiteral("android:")));
		QCOMPARE(environment.transport.SentTo(ADD_METHOD).front().accessToken, Session(record).accessToken);

		setup.SubmitActivationCode(QStringLiteral(" 55555 "));
		QTRY_COMPARE(setup.Stage(), SteamForge::GuardSetupStage::SUCCEEDED);
		SteamProtocol::FinalizeAddAuthenticatorRequest finalizeRequest;
		QVERIFY(finalizeRequest.ParseFromString(environment.transport.SentTo(FINALIZE_METHOD).front().protobufBody.toStdString()));
		QCOMPARE(QString::fromStdString(finalizeRequest.activation_code()), QStringLiteral("55555"));
		QCOMPARE(finalizeRequest.authenticator_code().size(), std::size_t{5});
		QVERIFY(finalizeRequest.validate_sms_code());
		QCOMPARE(finalizeRequest.steamid(), RECORD_STEAM_ID);

		const SteamForge::AuthenticatorRecord* active = environment.library.FindRecord(record.id);
		QVERIFY(!active->pendingActivation.has_value());
		QVERIFY(SteamForge::DetermineCapabilities(*active).canGenerateCodes);
		QVERIFY(SteamForge::DetermineCapabilities(*active).canSignConfirmations);
	}

	void WrongActivationCodeAsksAgain() {
		SetupEnvironment environment;
		const SteamForge::AuthenticatorRecord record = SignedInRecord();
		QVERIFY(environment.library.Add(record).has_value());
		AddAuthenticatorResponse(environment.transport);
		environment.transport.AddSteamError(FINALIZE_METHOD, SteamForge::SteamResult::TWO_FACTOR_ACTIVATION_CODE_MISMATCH);
		AddFinalizeSuccess(environment.transport);

		SteamForge::GuardSetup setup(environment.transport, environment.steamClock, environment.library, record, Session(record));
		setup.Start();
		QTRY_COMPARE(setup.Stage(), SteamForge::GuardSetupStage::AWAITING_ACTIVATION_CODE);
		setup.SubmitActivationCode(QStringLiteral("00000"));
		QTRY_VERIFY(setup.Failure().has_value());
		QCOMPARE(setup.Stage(), SteamForge::GuardSetupStage::AWAITING_ACTIVATION_CODE);
		QCOMPARE(setup.Failure()->kind, SteamForge::GuardSetupFailureKind::WRONG_ACTIVATION_CODE);
		QVERIFY(environment.library.FindRecord(record.id)->pendingActivation.has_value());

		setup.SubmitActivationCode(QStringLiteral("55555"));
		QTRY_COMPARE(setup.Stage(), SteamForge::GuardSetupStage::SUCCEEDED);
	}

	void LostFinalizeAnswerKeepsThePendingRecord() {
		SetupEnvironment environment;
		const SteamForge::AuthenticatorRecord record = SignedInRecord();
		QVERIFY(environment.library.Add(record).has_value());
		AddAuthenticatorResponse(environment.transport);

		SteamForge::GuardSetup setup(environment.transport, environment.steamClock, environment.library, record, Session(record));
		setup.Start();
		QTRY_COMPARE(setup.Stage(), SteamForge::GuardSetupStage::AWAITING_ACTIVATION_CODE);
		setup.SubmitActivationCode(QStringLiteral("55555"));
		QTRY_VERIFY(setup.Failure().has_value());
		QCOMPARE(setup.Failure()->kind, SteamForge::GuardSetupFailureKind::CONNECTION);
		QCOMPARE(setup.Stage(), SteamForge::GuardSetupStage::AWAITING_ACTIVATION_CODE);
		QVERIFY(environment.library.FindRecord(record.id)->pendingActivation.has_value());
		QCOMPARE(environment.library.FindRecord(record.id)->secrets.revocationCode, QStringLiteral("R11111"));
	}

	void ResumesAPendingActivationWithoutAddingAgain() {
		SetupEnvironment environment;
		SteamForge::AuthenticatorRecord record = SignedInRecord();
		record.secrets.sharedSecret = NewSharedSecret();
		record.secrets.revocationCode = QStringLiteral("R11111");
		record.pendingActivation = SteamForge::PendingActivation{.confirmType = SMS_CONFIRM_TYPE, .phoneNumberHint = {}};
		QVERIFY(environment.library.Add(record).has_value());
		AddFinalizeSuccess(environment.transport);

		SteamForge::GuardSetup setup(environment.transport, environment.steamClock, environment.library, record, Session(record));
		setup.Start();
		QCOMPARE(setup.Stage(), SteamForge::GuardSetupStage::AWAITING_ACTIVATION_CODE);
		setup.SubmitActivationCode(QStringLiteral("55555"));
		QTRY_COMPARE(setup.Stage(), SteamForge::GuardSetupStage::SUCCEEDED);
		QVERIFY(environment.transport.SentTo(ADD_METHOD).empty());
		QVERIFY(!environment.library.FindRecord(record.id)->pendingActivation.has_value());
	}

	void AccountWithAnotherAuthenticatorIsRefused() {
		SetupEnvironment environment;
		const SteamForge::AuthenticatorRecord record = SignedInRecord();
		QVERIFY(environment.library.Add(record).has_value());
		environment.transport.AddSteamError(ADD_METHOD, SteamForge::SteamResult::DUPLICATE_REQUEST);

		SteamForge::GuardSetup setup(environment.transport, environment.steamClock, environment.library, record, Session(record));
		setup.Start();
		QTRY_COMPARE(setup.Stage(), SteamForge::GuardSetupStage::FAILED);
		QCOMPARE(setup.Failure()->kind, SteamForge::GuardSetupFailureKind::ALREADY_PROTECTED);
		QVERIFY(environment.library.FindRecord(record.id)->secrets.sharedSecret.isEmpty());
		QVERIFY(!environment.library.FindRecord(record.id)->pendingActivation.has_value());
	}
};

QTEST_GUILESS_MAIN(GuardSetupTest)

#include "guard_setup_test.moc"
