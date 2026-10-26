#include "app/account_library.h"
#include "app/login_approval.h"
#include "support/fake_web_api_transport.h"

#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>

namespace {
constexpr quint64 RECORD_STEAM_ID = 76561198000000001;
constexpr quint64 CHALLENGE_CLIENT_ID = 9007199254740993;
constexpr int QR_MODULE_SIZE = 6;
constexpr int QR_MARGIN_MODULES = 4;
constexpr qint64 FAR_FUTURE_SECONDS = 4102444800;
constexpr qint64 PAST_SECONDS = 1000000000;
constexpr int UNAUTHORIZED_STATUS = 401;
constexpr int EPHEMERAL_SESSION = 0;
constexpr int PERSISTENT_SESSION = 1;

constexpr QLatin1StringView SESSION_INFO_METHOD{"GetAuthSessionInfo"};
constexpr QLatin1StringView CONFIRMATION_METHOD{"UpdateAuthSessionWithMobileConfirmation"};
constexpr QLatin1StringView ACCESS_TOKEN_METHOD{"GenerateAccessTokenForApp"};
constexpr QLatin1StringView PENDING_SESSIONS_METHOD{"GetAuthSessionsForAccount"};

QImage QrImage(const QString& text) {
	ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::QRCode);
	writer.setMargin(QR_MARGIN_MODULES);
	const ZXing::BitMatrix matrix = writer.encode(text.toStdString(), 0, 0);
	QImage image(matrix.width() * QR_MODULE_SIZE, matrix.height() * QR_MODULE_SIZE, QImage::Format_RGB32);
	image.fill(Qt::white);
	for (int row = 0; row < matrix.height(); ++row) {
		for (int column = 0; column < matrix.width(); ++column) {
			if (matrix.get(column, row)) {
				for (int i = 0; i < QR_MODULE_SIZE; ++i) {
					for (int j = 0; j < QR_MODULE_SIZE; ++j) {
						image.setPixel((column * QR_MODULE_SIZE) + j, (row * QR_MODULE_SIZE) + i, qRgb(0, 0, 0));
					}
				}
			}
		}
	}
	return image;
}

SteamForge::AuthenticatorRecord Record() {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source.importedAt = record.createdAt;
	record.account.steamId = RECORD_STEAM_ID;
	record.secrets.sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
	return record;
}

SteamForge::AccountSession Session(const SteamForge::AuthenticatorRecord& record, qint64 accessTokenExpiresAt) {
	return {
		.recordId = record.id,
		.steamId = RECORD_STEAM_ID,
		.accountName = QStringLiteral("synthetic_user"),
		.refreshToken = SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("derive")}, FAR_FUTURE_SECONDS),
		.accessToken = SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("mobile")}, accessTokenExpiresAt),
		.signedInAt = QDateTime::currentDateTimeUtc(),
	};
}

void AddSessionInfo(FakeWebApiTransport& transport, int version = 1, int requestedPersistence = EPHEMERAL_SESSION) {
	SteamProtocol::GetAuthSessionInfoResponse response;
	response.set_version(version);
	response.set_requested_persistence(requestedPersistence);
	response.set_ip("192.0.2.10");
	response.set_city("Example City");
	response.set_country("EX");
	response.set_platform_type(1);
	response.set_device_friendly_name("Synthetic PC");
	response.set_requestor_location_mismatch(true);
	transport.AddMessage(SESSION_INFO_METHOD, response);
}
} // namespace

class LoginApprovalTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ApprovesTheFoundRequestWithASignature() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		AddSessionInfo(transport);
		transport.AddResponse(CONFIRMATION_METHOD, QByteArray());
		const SteamForge::AuthenticatorRecord record = Record();
		const SteamForge::AccountSession session = Session(record, FAR_FUTURE_SECONDS);

		SteamForge::LoginApproval approval(transport, library, record, session);
		approval.ProcessScreenImages({QrImage(QStringLiteral("https://s.team/q/1/%1").arg(CHALLENGE_CLIENT_ID))});
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::AWAITING_DECISION);
		QCOMPARE(approval.SessionInfo().deviceFriendlyName, QStringLiteral("Synthetic PC"));
		QVERIFY(approval.SessionInfo().isRequestorLocationMismatch);
		QVERIFY(!approval.SessionInfo().isPersistenceRequested);
		QVERIFY(!approval.IsPasswordSignIn());
		QCOMPARE(transport.SentTo(SESSION_INFO_METHOD).front().accessToken, session.accessToken);
		QVERIFY(transport.SentTo(CONFIRMATION_METHOD).empty());

		approval.Approve(false);
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::APPROVED);
		SteamProtocol::UpdateAuthSessionWithMobileConfirmationRequest confirmation;
		QVERIFY(confirmation.ParseFromString(transport.SentTo(CONFIRMATION_METHOD).front().protobufBody.toStdString()));
		QVERIFY(confirmation.confirm());
		QCOMPARE(confirmation.persistence(), EPHEMERAL_SESSION);
		QCOMPARE(confirmation.client_id(), CHALLENGE_CLIENT_ID);
		QCOMPARE(confirmation.steamid(), RECORD_STEAM_ID);
		QCOMPARE(confirmation.version(), 1);
		const QByteArray expectedSignature = SteamForge::SignQrChallenge({.version = 1, .clientId = CHALLENGE_CLIENT_ID}, RECORD_STEAM_ID, record.secrets.sharedSecret);
		QCOMPARE(QByteArray::fromStdString(confirmation.signature()), expectedSignature);
	}

	void DenySendsANegativeDecision() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		AddSessionInfo(transport);
		transport.AddResponse(CONFIRMATION_METHOD, QByteArray());
		const SteamForge::AuthenticatorRecord record = Record();

		SteamForge::LoginApproval approval(transport, library, record, Session(record, FAR_FUTURE_SECONDS));
		approval.ProcessScreenImages({QrImage(QStringLiteral("https://s.team/q/1/42"))});
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::AWAITING_DECISION);
		approval.Deny();
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::DENIED);
		SteamProtocol::UpdateAuthSessionWithMobileConfirmationRequest confirmation;
		QVERIFY(confirmation.ParseFromString(transport.SentTo(CONFIRMATION_METHOD).front().protobufBody.toStdString()));
		QVERIFY(!confirmation.confirm());
		QCOMPARE(confirmation.persistence(), EPHEMERAL_SESSION);
	}

	void RefreshesAnExpiredAccessTokenFirst() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		const QString renewedAccessToken = SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS);
		SteamProtocol::GenerateAccessTokenForAppResponse tokens;
		tokens.set_access_token(renewedAccessToken.toStdString());
		transport.AddMessage(ACCESS_TOKEN_METHOD, tokens);
		AddSessionInfo(transport);
		const SteamForge::AuthenticatorRecord record = Record();

		SteamForge::LoginApproval approval(transport, library, record, Session(record, PAST_SECONDS));
		approval.ProcessScreenImages({QrImage(QStringLiteral("https://s.team/q/1/42"))});
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::AWAITING_DECISION);
		QCOMPARE(transport.SentTo(SESSION_INFO_METHOD).front().accessToken, renewedAccessToken);
		QCOMPARE(library.FindSession(record.id)->accessToken, renewedAccessToken);
	}

	void RequiresSignInWhenTheRefreshTokenExpired() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		const SteamForge::AuthenticatorRecord record = Record();
		SteamForge::AccountSession session = Session(record, PAST_SECONDS);
		session.refreshToken = SyntheticJwt(RECORD_STEAM_ID, {QStringLiteral("derive")}, PAST_SECONDS);

		SteamForge::LoginApproval approval(transport, library, record, session);
		approval.CheckPendingRequests();
		QCOMPARE(approval.Stage(), SteamForge::LoginApprovalStage::FAILED);
		QCOMPARE(approval.Failure()->kind, SteamForge::LoginApprovalFailureKind::SIGN_IN_REQUIRED);
		QVERIFY(transport.SentTo(ACCESS_TOKEN_METHOD).empty());
		QVERIFY(transport.SentTo(PENDING_SESSIONS_METHOD).empty());
	}

	void ApprovesARequestWaitingAfterAPasswordSignIn() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		SteamProtocol::GetAuthSessionsForAccountResponse pendingSessions;
		pendingSessions.add_client_ids(CHALLENGE_CLIENT_ID);
		pendingSessions.add_client_ids(7);
		transport.AddMessage(PENDING_SESSIONS_METHOD, pendingSessions);
		AddSessionInfo(transport, 1, PERSISTENT_SESSION);
		transport.AddResponse(CONFIRMATION_METHOD, QByteArray());
		const SteamForge::AuthenticatorRecord record = Record();

		SteamForge::LoginApproval approval(transport, library, record, Session(record, FAR_FUTURE_SECONDS));
		approval.CheckPendingRequests();
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::AWAITING_DECISION);
		QCOMPARE(approval.OtherPendingRequestCount(), std::size_t{1});
		QVERIFY(approval.SessionInfo().isPersistenceRequested);
		QVERIFY(approval.IsPasswordSignIn());
		// APK 3.10.9 declares this method constant (bConstMethod), and Steam answers HTTP 405 to POST.
		QCOMPARE(transport.SentTo(PENDING_SESSIONS_METHOD).front().httpMethod, SteamForge::WebApiHttpMethod::GET);
		QCOMPARE(transport.SentTo(SESSION_INFO_METHOD).front().httpMethod, SteamForge::WebApiHttpMethod::POST);
		SteamProtocol::GetAuthSessionInfoRequest infoRequest;
		QVERIFY(infoRequest.ParseFromString(transport.SentTo(SESSION_INFO_METHOD).front().protobufBody.toStdString()));
		QCOMPARE(infoRequest.client_id(), CHALLENGE_CLIENT_ID);

		approval.Approve(true);
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::APPROVED);
		SteamProtocol::UpdateAuthSessionWithMobileConfirmationRequest confirmation;
		QVERIFY(confirmation.ParseFromString(transport.SentTo(CONFIRMATION_METHOD).front().protobufBody.toStdString()));
		QCOMPARE(confirmation.client_id(), CHALLENGE_CLIENT_ID);
		QCOMPARE(confirmation.persistence(), PERSISTENT_SESSION);
		QCOMPARE(QByteArray::fromStdString(confirmation.signature()), SteamForge::SignQrChallenge({.version = 1, .clientId = CHALLENGE_CLIENT_ID}, RECORD_STEAM_ID, record.secrets.sharedSecret));
	}

	void ReportsWhenNothingIsWaiting() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		transport.AddMessage(PENDING_SESSIONS_METHOD, SteamProtocol::GetAuthSessionsForAccountResponse());
		const SteamForge::AuthenticatorRecord record = Record();

		SteamForge::LoginApproval approval(transport, library, record, Session(record, FAR_FUTURE_SECONDS));
		approval.CheckPendingRequests();
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::NOTHING_PENDING);
		QVERIFY(transport.SentTo(SESSION_INFO_METHOD).empty());
	}

	void RequiresSignInWhenSteamRejectsTheAccessToken() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		transport.AddHttpError(PENDING_SESSIONS_METHOD, UNAUTHORIZED_STATUS);
		const SteamForge::AuthenticatorRecord record = Record();

		SteamForge::LoginApproval approval(transport, library, record, Session(record, FAR_FUTURE_SECONDS));
		approval.CheckPendingRequests();
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::FAILED);
		QCOMPARE(approval.Failure()->kind, SteamForge::LoginApprovalFailureKind::SIGN_IN_REQUIRED);
	}

	void RefusesAPendingRequestOfUnknownVersion() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		SteamProtocol::GetAuthSessionsForAccountResponse pendingSessions;
		pendingSessions.add_client_ids(CHALLENGE_CLIENT_ID);
		transport.AddMessage(PENDING_SESSIONS_METHOD, pendingSessions);
		AddSessionInfo(transport, 2);
		const SteamForge::AuthenticatorRecord record = Record();

		SteamForge::LoginApproval approval(transport, library, record, Session(record, FAR_FUTURE_SECONDS));
		approval.CheckPendingRequests();
		QTRY_COMPARE(approval.Stage(), SteamForge::LoginApprovalStage::FAILED);
		QCOMPARE(approval.Failure()->kind, SteamForge::LoginApprovalFailureKind::UNSUPPORTED_VERSION);
		QVERIFY(transport.SentTo(CONFIRMATION_METHOD).empty());
	}

	void RejectsWithoutNetworkRequests_data() {
		QTest::addColumn<QList<QImage>>("images");
		QTest::addColumn<SteamForge::LoginApprovalFailureKind>("expectedFailure");
		QImage emptyScreen(320, 240, QImage::Format_RGB32);
		emptyScreen.fill(Qt::darkGray);
		QTest::newRow("no code") << QList<QImage>{emptyScreen} << SteamForge::LoginApprovalFailureKind::NO_QR_FOUND;
		QTest::newRow("two requests") << QList<QImage>{QrImage(QStringLiteral("https://s.team/q/1/1")), QrImage(QStringLiteral("https://s.team/q/1/2"))} << SteamForge::LoginApprovalFailureKind::SEVERAL_QR_FOUND;
		QTest::newRow("unknown version") << QList<QImage>{QrImage(QStringLiteral("https://s.team/q/2/42"))} << SteamForge::LoginApprovalFailureKind::UNSUPPORTED_VERSION;
	}

	void RejectsWithoutNetworkRequests() {
		QFETCH(QList<QImage>, images);
		QFETCH(SteamForge::LoginApprovalFailureKind, expectedFailure);
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions"))));
		FakeWebApiTransport transport;
		const SteamForge::AuthenticatorRecord record = Record();

		SteamForge::LoginApproval approval(transport, library, record, Session(record, FAR_FUTURE_SECONDS));
		approval.ProcessScreenImages(images);
		QCOMPARE(approval.Stage(), SteamForge::LoginApprovalStage::FAILED);
		QCOMPARE(approval.Failure()->kind, expectedFailure);
		QCOMPARE(transport.SentCount(), std::size_t{0});
	}
};

QTEST_MAIN(LoginApprovalTest)

#include "login_approval_test.moc"
