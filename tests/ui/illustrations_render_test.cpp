#include "app/account_library.h"
#include "app/account_profiles.h"
#include "app/application_settings.h"
#include "app/cloud_sync.h"
#include "app/confirmation_monitor.h"
#include "app/google_connection.h"
#include "app/interface_translator.h"
#include "app/storage_encryption.h"
#include "platform/application_icons.h"
#include "platform/data_location.h"
#include "platform/window_frame.h"
#include "steam/community_profile.h"
#include "steam/steam_clock.h"
#include "steam/steam_image_cache.h"
#include "support/fake_community_transport.h"
#include "support/fake_google_transport.h"
#include "support/fake_key_store.h"
#include "support/fake_web_api_transport.h"
#include "support/render_language.h"
#include "support/synthetic_tokens.h"
#include "ui/account_list_view.h"
#include "ui/confirmations_dialog.h"
#include "ui/google_connect_dialog.h"
#include "ui/guard_setup_dialog.h"
#include "ui/import_dialog.h"
#include "ui/item_tiles.h"
#include "ui/login_approval_dialog.h"
#include "ui/login_dialog.h"
#include "ui/main_window.h"
#include "ui/record_data_dialog.h"
#include "ui/record_presentation.h"
#include "ui/settings_dialog.h"
#include "ui/steam_guard_qr_dialog.h"
#include "ui/success_dialog.h"
#include "ui/theme.h"

// A temporary render of the illustrated empty states for a visual check.
namespace {
constexpr qint64 FAR_FUTURE_SECONDS = 4102444800;
constexpr qint64 PAST_SECONDS = 1600000000;
constexpr quint64 RENDER_STEAM_ID = 76561201960265729;
constexpr quint64 EXPIRED_STEAM_ID = 76561201960265730;
constexpr QLatin1StringView LIST_PATH{"mobileconf/getlist"};
constexpr int LIST_RESPONSE_COUNT = 6;
// Ordinary players with official avatars of the Steam avatar gallery, loaded from the Steam CDN.
struct DemoAccount {
	quint64 steamId = 0;
	QString accountName;
	QString nickname;
	QLatin1StringView avatarHash;
};

QByteArray DemoProfileXml(const DemoAccount& account) {
	const QString avatarUrl = QStringLiteral("https://avatars.fastly.steamstatic.com/%1_full.jpg").arg(account.avatarHash);
	return QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID64>%1</steamID64><steamID><![CDATA[%2]]></steamID><avatarFull><![CDATA[%3]]></avatarFull></profile>)").arg(account.steamId).arg(account.nickname, avatarUrl).toUtf8();
}
} // namespace

class IllustrationsRenderTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void initTestCase() { // NOLINT(readability-identifier-naming): QtTest calls this slot by its name.
		QApplication::setWindowIcon(SteamForge::ApplicationIcon());
	}

	void RenderEmptyStates() {
		const QString renderDirectory = qEnvironmentVariable("STEAMFORGE_RENDER_DIRECTORY");
		if (renderDirectory.isEmpty()) {
			QSKIP("Set STEAMFORGE_RENDER_DIRECTORY to render the empty states.");
		}
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);
		SteamForge::InstallWindowFrameStyling(application);
		SteamForge::InterfaceTranslator translator;

		const QTemporaryDir dataDirectory;
		const QDir dataRoot(dataDirectory.path());
		SteamForge::AccountLibrary emptyLibrary(SteamForge::AuthenticatorStore(dataRoot.filePath(QStringLiteral("empty_accounts"))), SteamForge::SessionStore(dataRoot.filePath(QStringLiteral("empty_sessions"))));
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(dataRoot.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(dataRoot.filePath(QStringLiteral("sessions"))));
		SteamForge::AuthenticatorRecord record;
		record.id = QUuid::createUuid();
		record.account.steamId = RENDER_STEAM_ID;
		record.account.accountName = QStringLiteral("artie2008");
		record.createdAt = QDateTime::currentDateTimeUtc();
		record.secrets.sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
		record.secrets.identitySecret = QByteArray::fromBase64("FBMSERAPDg0MCwoJCAcGBQQDAgE=");
		record.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-000000000001");
		QVERIFY(library.Add(record).has_value());
		const SteamForge::AccountSession session{.recordId = record.id, .steamId = RENDER_STEAM_ID, .accountName = QStringLiteral("artie2008"), .refreshToken = SyntheticJwt(RENDER_STEAM_ID, {QStringLiteral("derive")}, FAR_FUTURE_SECONDS), .accessToken = SyntheticJwt(RENDER_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS), .signedInAt = QDateTime::currentDateTimeUtc()};
		QVERIFY(library.SaveSession(session).has_value());
		SteamForge::AuthenticatorRecord expiredRecord = record;
		expiredRecord.id = QUuid::createUuid();
		expiredRecord.account.steamId = EXPIRED_STEAM_ID;
		expiredRecord.account.accountName = QStringLiteral("artie_old");
		QVERIFY(library.Add(expiredRecord).has_value());
		QVERIFY(library.SaveSession({.recordId = expiredRecord.id, .steamId = EXPIRED_STEAM_ID, .accountName = QStringLiteral("artie_old"), .refreshToken = SyntheticJwt(EXPIRED_STEAM_ID, {QStringLiteral("derive")}, PAST_SECONDS), .accessToken = SyntheticJwt(EXPIRED_STEAM_ID, {QStringLiteral("mobile")}, PAST_SECONDS), .signedInAt = QDateTime::currentDateTimeUtc()}).has_value());
		SteamForge::AuthenticatorRecord unprotectedRecord = record;
		unprotectedRecord.secrets = {};

		FakeWebApiTransport webApiTransport;
		FakeCommunityTransport communityTransport;
		for (int i = 0; i < LIST_RESPONSE_COUNT; ++i) {
			communityTransport.AddJson(LIST_PATH, QJsonObject{{QStringLiteral("success"), true}, {QStringLiteral("conf"), QJsonArray()}});
		}
		SteamForge::SteamClock clock;
		SteamForge::SteamImageCache imageCache;
		SteamForge::ConfirmationMonitor emptyMonitor(webApiTransport, communityTransport, emptyLibrary, clock);
		SteamForge::ConfirmationMonitor monitor(webApiTransport, communityTransport, library, clock);
		SteamForge::ApplicationSettings settings(dataRoot.filePath(QStringLiteral("settings.ini")));

		// The fake transport has no answers, so profiles of the synthetic accounts are not asked from Steam.
		FakeCommunityTransport profileTransport;
		SteamForge::AccountProfiles profiles(profileTransport, imageCache, dataDirectory.filePath(QStringLiteral("profiles.json")));
		FakeKeyStore keyStore;
		SteamForge::StorageEncryption encryption(keyStore, emptyLibrary, dataDirectory.filePath(QStringLiteral("encryption.json")));
		encryption.PrepareLibrary();
		FakeGoogleTransport googleTransport;
		SteamForge::GoogleConnection googleConnection(googleTransport, encryption, SteamForge::GoogleClient{.clientId = QStringLiteral("render-client"), .clientSecret = {}}, SteamForge::DefaultGoogleEndpoints(), dataDirectory.filePath(QStringLiteral("google.json")));
		SteamForge::CloudSync cloudSync(googleConnection, emptyLibrary);
		SteamForge::MainWindow mainWindow(emptyLibrary, clock, emptyMonitor, imageCache, profiles, encryption, googleConnection, cloudSync, settings, SteamForge::DataLocation{.directoryPath = dataDirectory.path(), .isPortable = false});
		mainWindow.show();
		SteamForge::ConfirmationsDialog dialog(monitor, imageCache, record, SteamForge::RecordDisplayName(record), QImage());
		SteamForge::ConfirmationsDialog signInDialog(monitor, imageCache, expiredRecord, SteamForge::RecordDisplayName(expiredRecord), QImage());
		SteamForge::GuardSetupDialog guardSetupDialog(library, clock, unprotectedRecord, SteamForge::RecordDisplayName(unprotectedRecord), session);
		// Not shown: showing it would ask Steam for pending sign-ins.
		SteamForge::LoginApprovalDialog loginApprovalDialog(library, record, SteamForge::RecordDisplayName(record), session);
		monitor.Start();
		dialog.show();
		signInDialog.show();
		guardSetupDialog.show();

		const std::array languages{std::make_pair(SteamForge::InterfaceLanguage::RUSSIAN, QStringLiteral("ru")), std::make_pair(SteamForge::InterfaceLanguage::ENGLISH, QStringLiteral("en"))};
		for (const auto& [language, suffix] : languages) {
			QVERIFY(translator.Apply(language));
			QTest::qWait(1500);
			QVERIFY(mainWindow.grab().save(QDir(renderDirectory).filePath(QStringLiteral("welcome_%1.png").arg(suffix))));
			QVERIFY(dialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("no_confirmations_%1.png").arg(suffix))));
			QVERIFY(signInDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("sign_in_required_%1.png").arg(suffix))));
			QVERIFY(guardSetupDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("guard_setup_%1.png").arg(suffix))));
			loginApprovalDialog.adjustSize();
			QVERIFY(loginApprovalDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("qr_approval_%1.png").arg(suffix))));
		}
	}

	void RenderErrorTexts() {
		const QString renderDirectory = qEnvironmentVariable("STEAMFORGE_RENDER_DIRECTORY");
		if (renderDirectory.isEmpty()) {
			QSKIP("Set STEAMFORGE_RENDER_DIRECTORY to list the error texts.");
		}
		struct ErrorSample {
			QString name;
			SteamForge::WebApiError error;
		};
		const auto makeError = [](SteamForge::WebApiErrorKind kind, int httpStatus, SteamForge::SteamResult steamResult, const QString& message) {
			return SteamForge::WebApiError{.kind = kind, .httpStatus = httpStatus, .steamResult = steamResult, .message = message};
		};
		const std::vector<ErrorSample> samples{
			{.name = QStringLiteral("no network"), .error = makeError(SteamForge::WebApiErrorKind::NETWORK, 0, SteamForge::SteamResult::UNKNOWN, QStringLiteral("HostNotFoundError"))},
			{.name = QStringLiteral("timeout"), .error = makeError(SteamForge::WebApiErrorKind::TIMEOUT, 0, SteamForge::SteamResult::UNKNOWN, QStringLiteral("OperationCanceledError"))},
			{.name = QStringLiteral("HTTP 429"), .error = makeError(SteamForge::WebApiErrorKind::HTTP_STATUS, 429, SteamForge::SteamResult::UNKNOWN, {})},
			{.name = QStringLiteral("HTTP 401"), .error = makeError(SteamForge::WebApiErrorKind::HTTP_STATUS, 401, SteamForge::SteamResult::UNKNOWN, {})},
			{.name = QStringLiteral("HTTP 503"), .error = makeError(SteamForge::WebApiErrorKind::HTTP_STATUS, 503, SteamForge::SteamResult::UNKNOWN, {})},
			{.name = QStringLiteral("HTTP 404"), .error = makeError(SteamForge::WebApiErrorKind::HTTP_STATUS, 404, SteamForge::SteamResult::UNKNOWN, {})},
			{.name = QStringLiteral("EResult 93"), .error = makeError(SteamForge::WebApiErrorKind::STEAM_RESULT, 0, SteamForge::SteamResult::TIME_NOT_SYNCED, {})},
			{.name = QStringLiteral("EResult 84"), .error = makeError(SteamForge::WebApiErrorKind::STEAM_RESULT, 0, SteamForge::SteamResult::RATE_LIMIT_EXCEEDED, {})},
			{.name = QStringLiteral("EResult 15"), .error = makeError(SteamForge::WebApiErrorKind::STEAM_RESULT, 0, SteamForge::SteamResult::ACCESS_DENIED, {})},
			{.name = QStringLiteral("EResult 2"), .error = makeError(SteamForge::WebApiErrorKind::STEAM_RESULT, 0, SteamForge::SteamResult::FAIL, {})},
			{.name = QStringLiteral("unreadable answer"), .error = makeError(SteamForge::WebApiErrorKind::MALFORMED_RESPONSE, 0, SteamForge::SteamResult::UNKNOWN, {})},
		};
		struct Situation {
			const char* context;
			const char* whatFailed;
			const char* whatNext;
		};
		const std::array situations{
			Situation{.context = "SteamForge::ConfirmationsDialog", .whatFailed = "Confirmations could not be loaded.", .whatNext = "We'll try again automatically."},
			Situation{.context = "SteamForge::ConfirmationsDialog", .whatFailed = "It is unknown whether Steam carried out the decision.", .whatNext = "The list was loaded again: an entry that is still listed was not processed."},
			Situation{.context = "SteamForge::ConfirmationsDialog", .whatFailed = "Steam did not carry out the decision.", .whatNext = "The list was loaded again."},
			Situation{.context = "SteamForge::LoginDialog", .whatFailed = "Could not sign in to Steam.", .whatNext = ""},
			Situation{.context = "SteamForge::LoginApprovalDialog", .whatFailed = "The sign-in request could not be handled.", .whatNext = ""},
			Situation{.context = "SteamForge::GuardSetupDialog", .whatFailed = "Steam Guard could not be set up.", .whatNext = ""},
		};
		const auto describe = [](const Situation& situation, const SteamForge::WebApiError& error) {
			const QString whatNext = QString::fromLatin1(situation.whatNext).isEmpty() ? QString() : QCoreApplication::translate(situation.context, situation.whatNext);
			return SteamForge::DescribeWebApiError(QCoreApplication::translate(situation.context, situation.whatFailed), error, whatNext);
		};

		SteamForge::InterfaceTranslator translator;
		QFile output(QDir(renderDirectory).filePath(QStringLiteral("error_texts.md")));
		QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
		QTextStream stream(&output);
		const std::array languages{std::make_pair(SteamForge::InterfaceLanguage::RUSSIAN, QStringLiteral("ru")), std::make_pair(SteamForge::InterfaceLanguage::ENGLISH, QStringLiteral("en"))};
		for (const auto& [language, suffix] : languages) {
			QVERIFY(translator.Apply(language));
			stream << "## " << suffix << "\n\nCauses, while loading confirmations:\n\n";
			for (const ErrorSample& sample : samples) {
				stream << "- " << sample.name << ": " << describe(situations.front(), sample.error) << "\n";
			}
			stream << "\nSituations, with no network:\n\n";
			for (const Situation& situation : situations) {
				stream << "- " << situation.context << ": " << describe(situation, samples.front().error) << "\n";
			}
			stream << "\n";
		}
	}

	void RenderLocked_data() {
		QTest::addColumn<SteamForge::KeyProtection>("protection");
		QTest::addColumn<QString>("protectionName");
		QTest::addColumn<QString>("fileName");
		QTest::newRow("Windows Hello") << SteamForge::KeyProtection::WINDOWS_HELLO << QStringLiteral("windows-hello") << QStringLiteral("locked.png");
		QTest::newRow("keyring password") << SteamForge::KeyProtection::KEYRING_PASSWORD << QStringLiteral("keyring-password") << QStringLiteral("locked_keyring.png");
	}

	void RenderLocked() {
		QFETCH(SteamForge::KeyProtection, protection);
		QFETCH(QString, protectionName);
		QFETCH(QString, fileName);
		const QString renderDirectory = qEnvironmentVariable("STEAMFORGE_RENDER_DIRECTORY");
		if (renderDirectory.isEmpty()) {
			QSKIP("Set STEAMFORGE_RENDER_DIRECTORY to render the locked window.");
		}
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);
		SteamForge::InterfaceTranslator translator;
		QVERIFY(translator.Apply(RenderLanguage()));

		const QTemporaryDir dataDirectory;
		const QDir dataRoot(dataDirectory.path());
		QFile stateFile(dataRoot.filePath(QStringLiteral("encryption.json")));
		QVERIFY(stateFile.open(QIODevice::WriteOnly));
		stateFile.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("steamforge-encryption")}, {QStringLiteral("formatVersion"), 1}, {QStringLiteral("state"), QStringLiteral("enabled")}, {QStringLiteral("vaultId"), QStringLiteral("render")}, {QStringLiteral("protection"), protectionName}}).toJson());
		stateFile.close();
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(dataRoot.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(dataRoot.filePath(QStringLiteral("sessions"))));
		FakeKeyStore keyStore;
		keyStore.preferredProtection = protection;
		SteamForge::StorageEncryption encryption(keyStore, library, stateFile.fileName());
		encryption.PrepareLibrary();
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::LOCKED);

		FakeWebApiTransport webApiTransport;
		FakeCommunityTransport communityTransport;
		SteamForge::SteamClock clock;
		SteamForge::SteamImageCache imageCache;
		SteamForge::ConfirmationMonitor monitor(webApiTransport, communityTransport, library, clock);
		SteamForge::ApplicationSettings settings(dataRoot.filePath(QStringLiteral("settings.ini")));
		FakeCommunityTransport profileTransport;
		SteamForge::AccountProfiles profiles(profileTransport, imageCache, dataRoot.filePath(QStringLiteral("profiles.json")));
		FakeGoogleTransport googleTransport;
		SteamForge::GoogleConnection googleConnection(googleTransport, encryption, SteamForge::GoogleClient{.clientId = QStringLiteral("render-client"), .clientSecret = {}}, SteamForge::DefaultGoogleEndpoints(), dataDirectory.filePath(QStringLiteral("google.json")));
		SteamForge::CloudSync cloudSync(googleConnection, library);
		SteamForge::MainWindow mainWindow(library, clock, monitor, imageCache, profiles, encryption, googleConnection, cloudSync, settings, SteamForge::DataLocation{.directoryPath = dataDirectory.path(), .isPortable = false});
		mainWindow.show();
		QTest::qWait(500);
		QVERIFY(mainWindow.grab().save(QDir(renderDirectory).filePath(fileName)));
	}

	void RenderComfort() {
		const QString renderDirectory = qEnvironmentVariable("STEAMFORGE_RENDER_DIRECTORY");
		if (renderDirectory.isEmpty()) {
			QSKIP("Set STEAMFORGE_RENDER_DIRECTORY to render the moments of success.");
		}
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);
		SteamForge::InterfaceTranslator translator;
		QVERIFY(translator.Apply(RenderLanguage()));

		SteamForge::SuccessDialog movedNotice(QCoreApplication::translate("SteamForge::LoginDialog", "Steam Guard moved"), QCoreApplication::translate("SteamForge::LoginDialog", "Steam Guard of %1 is now in SteamForge. One step is left: enter the password once more, and the code is entered automatically.").arg(QStringLiteral("Artie")));
		movedNotice.SetKeptValue(QCoreApplication::translate("SteamForge::LoginDialog", "New recovery code"), QStringLiteral("R12345"), QCoreApplication::translate("SteamForge::LoginDialog", "Write it down and keep it apart from this computer: with it Steam Guard can be removed from the account if SteamForge is lost. The account details show it as well."));
		movedNotice.show();
		QTest::qWait(300);
		QVERIFY(movedNotice.grab().save(QDir(renderDirectory).filePath(QStringLiteral("guard_moved.png"))));

		QWidget tilesWindow;
		tilesWindow.setStyleSheet(QStringLiteral("background-color: #1b2838;"));
		auto* tilesLayout = new QVBoxLayout(&tilesWindow);
		tilesLayout->addWidget(new SteamForge::ItemTilePlaceholders(&tilesWindow));
		tilesWindow.resize(560, 160);
		tilesWindow.show();
		QTest::qWait(500);
		QVERIFY(tilesWindow.grab().save(QDir(renderDirectory).filePath(QStringLiteral("loading_tiles.png"))));

		const QTemporaryDir dataDirectory;
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(dataDirectory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(dataDirectory.filePath(QStringLiteral("sessions"))));
		FakeKeyStore keyStore;
		SteamForge::StorageEncryption encryption(keyStore, library, dataDirectory.filePath(QStringLiteral("encryption.json")));
		encryption.PrepareLibrary();
		SteamForge::SteamClock clock;
		SteamForge::AuthenticatorRecord newAccount;
		newAccount.id = QUuid::createUuid();
		SteamForge::LoginDialog loginDialog(library, clock, encryption, newAccount, QString());
		loginDialog.show();
		QTest::qWait(300);
		QVERIFY(loginDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("login_unencrypted.png"))));

		const SteamForge::SteamGuardLinks qrLinks{
			.steamEncoderLink = QStringLiteral("otpauth://totp/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&issuer=Steam&digits=5&encoder=steam"),
			.steamTypeLink = QStringLiteral("otpauth://steam/Steam:synthetic_user?secret=AEBAGBAFAYDQQCIKBMGA2DQPCAIREEYU&issuer=Steam&digits=5"),
		};
		SteamForge::SteamGuardQrDialog qrDialog(QStringLiteral("Artie"), qrLinks);
		qrDialog.show();
		QTest::qWait(300);
		QVERIFY(qrDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("steam_guard_qr_hidden.png"))));
		const QList<QPushButton*> qrButtons = qrDialog.findChildren<QPushButton*>();
		const auto showQrButton = std::ranges::find_if(qrButtons, [](const QPushButton* button) {
			return button->text() == QCoreApplication::translate("SteamForge::SteamGuardQrDialog", "Show QR code");
		});
		QVERIFY(showQrButton != qrButtons.end());
		(*showQrButton)->click();
		QTest::qWait(100);
		QVERIFY(qrDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("steam_guard_qr.png"))));
		const auto steamTypeTab = std::ranges::find_if(qrButtons, [](const QPushButton* button) {
			return button->text() == QCoreApplication::translate("SteamForge::SteamGuardQrDialog", "Aegis, Ente, 2FAS for Android");
		});
		QVERIFY(steamTypeTab != qrButtons.end());
		(*steamTypeTab)->click();
		QTest::qWait(100);
		QVERIFY(qrDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("steam_guard_qr_steam_type.png"))));
	}

	void RenderTour() {
		const QString renderDirectory = qEnvironmentVariable("STEAMFORGE_RENDER_DIRECTORY");
		if (renderDirectory.isEmpty()) {
			QSKIP("Set STEAMFORGE_RENDER_DIRECTORY to render the screens.");
		}
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);
		SteamForge::InterfaceTranslator translator;
		QVERIFY(translator.Apply(RenderLanguage()));
		const QTemporaryDir dataDirectory;
		const QDir dataRoot(dataDirectory.path());
		SteamForge::AccountLibrary library(SteamForge::AuthenticatorStore(dataRoot.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(dataRoot.filePath(QStringLiteral("sessions"))));
		SteamForge::AuthenticatorRecord fullRecord;
		fullRecord.id = QUuid::createUuid();
		fullRecord.account.steamId = RENDER_STEAM_ID;
		fullRecord.account.accountName = QStringLiteral("artie2008");
		fullRecord.createdAt = QDateTime::currentDateTimeUtc();
		fullRecord.source.origin = SteamForge::RecordOrigin::MA_FILE;
		fullRecord.source.importedAt = fullRecord.createdAt;
		fullRecord.secrets.sharedSecret = QByteArray::fromBase64("AQIDBAUGBwgJCgsMDQ4PEBESExQ=");
		fullRecord.secrets.identitySecret = QByteArray::fromBase64("FBMSERAPDg0MCwoJCAcGBQQDAgE=");
		fullRecord.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-000000000001");
		fullRecord.secrets.revocationCode = QStringLiteral("R12345");
		QVERIFY(library.Add(fullRecord).has_value());
		QVERIFY(library.SaveSession({.recordId = fullRecord.id, .steamId = RENDER_STEAM_ID, .accountName = QStringLiteral("artie2008"), .refreshToken = SyntheticJwt(RENDER_STEAM_ID, {QStringLiteral("derive")}, FAR_FUTURE_SECONDS), .accessToken = SyntheticJwt(RENDER_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS), .signedInAt = QDateTime::currentDateTimeUtc()}).has_value());
		SteamForge::AuthenticatorRecord codesOnlyRecord;
		codesOnlyRecord.id = QUuid::createUuid();
		codesOnlyRecord.label = QStringLiteral("smurf");
		codesOnlyRecord.createdAt = QDateTime::currentDateTimeUtc();
		codesOnlyRecord.source.origin = SteamForge::RecordOrigin::OTPAUTH_URI;
		codesOnlyRecord.source.importedAt = codesOnlyRecord.createdAt;
		codesOnlyRecord.secrets.sharedSecret = QByteArray::fromBase64("CQoLDA0ODxAREhMUFRYXGBkaGxw=");
		QVERIFY(library.Add(codesOnlyRecord).has_value());
		const std::array demoAccounts{
			DemoAccount{.steamId = RENDER_STEAM_ID, .accountName = QStringLiteral("artie2008"), .nickname = QStringLiteral("Artie"), .avatarHash = QLatin1StringView("4264b561c93fcd0d4c832c1137229587bbb5f8d7")},
			DemoAccount{.steamId = RENDER_STEAM_ID + 11, .accountName = QStringLiteral("meatball_cs"), .nickname = QStringLiteral("meatball"), .avatarHash = QLatin1StringView("2db6e5f11d2eb0ff1410cd6353f771da213124a0")},
			DemoAccount{.steamId = RENDER_STEAM_ID + 12, .accountName = QStringLiteral("foxy_plays"), .nickname = QStringLiteral("Foxy"), .avatarHash = QLatin1StringView("95cc644e231f05cf0590311be14b2924ed66f66b")},
		};
		for (const DemoAccount& account : demoAccounts | std::views::drop(1)) {
			SteamForge::AuthenticatorRecord friendRecord;
			friendRecord.id = QUuid::createUuid();
			friendRecord.account.steamId = account.steamId;
			friendRecord.account.accountName = account.accountName;
			friendRecord.createdAt = QDateTime::currentDateTimeUtc();
			friendRecord.source.origin = SteamForge::RecordOrigin::MA_FILE;
			friendRecord.source.importedAt = friendRecord.createdAt;
			friendRecord.secrets.sharedSecret = QCryptographicHash::hash(account.accountName.toUtf8(), QCryptographicHash::Sha1);
			QVERIFY(library.Add(friendRecord).has_value());
		}

		FakeWebApiTransport webApiTransport;
		FakeCommunityTransport communityTransport;
		for (int i = 0; i < LIST_RESPONSE_COUNT; ++i) {
			communityTransport.AddJson(LIST_PATH, QJsonObject{{QStringLiteral("success"), true}, {QStringLiteral("conf"), QJsonArray()}});
		}
		const QJsonObject eligibility{{QStringLiteral("allowed"), 1}};
		communityTransport.AddCookies(QStringLiteral("market/eligibilitycheck/"), QList<QNetworkCookie>{QNetworkCookie(QByteArray("webTradeEligibility"), QJsonDocument(eligibility).toJson(QJsonDocument::Compact).toPercentEncoding())});
		SteamForge::SteamClock clock;
		SteamForge::SteamImageCache imageCache;
		SteamForge::ConfirmationMonitor monitor(webApiTransport, communityTransport, library, clock);
		SteamForge::ApplicationSettings settings(dataRoot.filePath(QStringLiteral("settings.ini")));
		FakeCommunityTransport profileTransport;
		for (const DemoAccount& account : demoAccounts) {
			profileTransport.AddResponse(SteamForge::CommunityProfilePath(account.steamId), DemoProfileXml(account));
		}
		SteamForge::AccountProfiles profiles(profileTransport, imageCache, dataDirectory.filePath(QStringLiteral("profiles.json")));
		FakeKeyStore keyStore;
		SteamForge::StorageEncryption encryption(keyStore, library, dataDirectory.filePath(QStringLiteral("encryption.json")));
		encryption.PrepareLibrary();
		FakeGoogleTransport googleTransport;
		SteamForge::GoogleConnection googleConnection(googleTransport, encryption, SteamForge::GoogleClient{.clientId = QStringLiteral("render-client"), .clientSecret = {}}, SteamForge::DefaultGoogleEndpoints(), dataDirectory.filePath(QStringLiteral("google.json")));
		SteamForge::CloudSync cloudSync(googleConnection, library);
		SteamForge::MainWindow mainWindow(library, clock, monitor, imageCache, profiles, encryption, googleConnection, cloudSync, settings, SteamForge::DataLocation{.directoryPath = dataDirectory.path(), .isPortable = false});
		monitor.Start();
		SteamForge::AccountConfirmations* fullRecordConfirmations = monitor.Find(fullRecord.id);
		QVERIFY(fullRecordConfirmations != nullptr);
		fullRecordConfirmations->Refresh();
		mainWindow.show();
		QTest::qWait(800);
		auto* accountList = mainWindow.findChild<SteamForge::AccountListView*>();
		QVERIFY(accountList != nullptr);
		accountList->SelectRecord(fullRecord.id);
		QTest::qWait(3500);
		QVERIFY(mainWindow.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_full_account.png"))));
		QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		QTest::qWait(300);
		QVERIFY(mainWindow.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_encrypted.png"))));
		accountList->SelectRecord(codesOnlyRecord.id);
		QTest::qWait(300);
		QVERIFY(mainWindow.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_codes_only.png"))));
		mainWindow.activateWindow();
		QVERIFY(QTest::qWaitForWindowActive(&mainWindow));
		bool isUpdateOfferSaved = false;
		mainWindow.ShowAvailableUpdate(QVersionNumber(0, 2, 0));
		// The offer runs its own event loop, so it is captured and closed from a timer inside that loop.
		QTimer::singleShot(500, &mainWindow, [&renderDirectory, &isUpdateOfferSaved]() {
			auto* updateOffer = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
			if (updateOffer == nullptr) {
				return;
			}
			isUpdateOfferSaved = updateOffer->grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_update.png")));
			updateOffer->reject();
		});
		QTRY_VERIFY(isUpdateOfferSaved);

		SteamForge::AuthenticatorRecord newAccount;
		newAccount.id = QUuid::createUuid();
		SteamForge::LoginDialog loginDialog(library, clock, encryption, newAccount, SteamForge::RecordDisplayName(newAccount), SteamForge::LoginPurpose::SIGN_IN);
		loginDialog.show();
		SteamForge::LoginDialog recordLoginDialog(library, clock, encryption, codesOnlyRecord, SteamForge::RecordDisplayName(codesOnlyRecord), SteamForge::LoginPurpose::SIGN_IN);
		recordLoginDialog.show();
		SteamForge::ImportDialog importDialog(library);
		importDialog.show();
		SteamForge::SettingsDialog settingsDialog(std::nullopt, true, SteamForge::DataLocation{.directoryPath = dataDirectory.path(), .isPortable = false}, encryption, googleConnection, SteamForge::SettingsDialog::CloudAccountsSource{.sync = cloudSync, .library = library, .titleProvider = SteamForge::RecordDisplayName}, SteamForge::SettingsSection::ENCRYPTION);
		settingsDialog.show();
		SteamForge::RecordDataDialog recordDataDialog(library, codesOnlyRecord, SteamForge::RecordDisplayName(codesOnlyRecord), std::nullopt);
		recordDataDialog.show();
		QTest::qWait(500);
		QVERIFY(loginDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_login_new.png"))));
		QVERIFY(recordLoginDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_login_record.png"))));
		QVERIFY(importDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_import.png"))));
		QVERIFY(settingsDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_settings.png"))));
		settingsDialog.ShowSection(SteamForge::SettingsSection::GOOGLE);
		QTest::qWait(100);
		QVERIFY(settingsDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_settings_google.png"))));
		QVERIFY(recordDataDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_record_data.png"))));

		// The Google connection window: what goes to Google, then the recovery key written down or typed in.
		const SteamForge::GoogleConnectDialog::EncryptionStarter noEncryption = []() {
			return false;
		};
		SteamForge::GoogleConnectDialog googleIntroDialog(googleConnection, cloudSync, library, SteamForge::RecordDisplayName, noEncryption, false);
		googleIntroDialog.show();
		QTest::qWait(300);
		QVERIFY(googleIntroDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_google_intro.png"))));
		googleIntroDialog.hide();
		SteamForge::GoogleConnectDialog googleKeyDialog(googleConnection, cloudSync, library, SteamForge::RecordDisplayName, noEncryption, true);
		googleKeyDialog.show();
		Q_EMIT googleConnection.NewVaultCreated(QStringLiteral("K7QX-M2PA-VR4D-9TBN-HC3W-ZJ5E-LF6S-UY8G").replace(u'9', u'R').replace(u'8', u'B'));
		QTest::qWait(300);
		QVERIFY(googleKeyDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_google_new_key.png"))));
		Q_EMIT googleConnection.RecoveryKeyNeeded();
		QTest::qWait(300);
		QVERIFY(googleKeyDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_google_enter_key.png"))));
		Q_EMIT googleConnection.Connected();
		QTest::qWait(300);
		QVERIFY(googleKeyDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_google_choose.png"))));
		const QList<QPushButton*> googleButtons = googleKeyDialog.findChildren<QPushButton*>();
		const auto notNowButton = std::ranges::find_if(googleButtons, [](const QPushButton* button) {
			return button->text() == QCoreApplication::translate("SteamForge::GoogleConnectDialog", "Not now");
		});
		QVERIFY(notNowButton != googleButtons.end());
		(*notNowButton)->click();
		QTest::qWait(300);
		QVERIFY(googleKeyDialog.grab().save(QDir(renderDirectory).filePath(QStringLiteral("tour_google_done.png"))));
		googleKeyDialog.hide();
	}
};

QTEST_MAIN(IllustrationsRenderTest)

#include "illustrations_render_test.moc"
