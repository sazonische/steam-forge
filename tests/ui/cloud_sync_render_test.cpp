#include "app/account_profiles.h"
#include "app/application_settings.h"
#include "app/confirmation_monitor.h"
#include "platform/application_icons.h"
#include "platform/data_location.h"
#include "platform/window_frame.h"
#include "steam/community_profile.h"
#include "steam/steam_clock.h"
#include "steam/steam_image_cache.h"
#include "support/cloud_computer.h"
#include "support/demo_profiles.h"
#include "support/fake_community_transport.h"
#include "support/fake_web_api_transport.h"
#include "support/render_language.h"
#include "support/synthetic_tokens.h"
#include "ui/account_details_view.h"
#include "ui/account_list_view.h"
#include "ui/cloud_conflict_dialog.h"
#include "ui/import_dialog.h"
#include "ui/main_window.h"
#include "ui/record_presentation.h"
#include "ui/settings_dialog.h"
#include "ui/success_dialog.h"
#include "ui/theme.h"

// A temporary render of Google sync on a computer with an account in Google, one only here, another Steam Guard of
// an account in Google and one left only in Google, for a visual check.
namespace {
constexpr quint64 SYNCED_STEAM_ID = 76561201960265741;
constexpr quint64 LOCAL_STEAM_ID = 76561201960265742;
constexpr quint64 CONFLICT_STEAM_ID = 76561201960265743;
constexpr quint64 AWAY_STEAM_ID = 76561201960265744;
// The players of the other frames of the README.
constexpr quint64 ARTIE_STEAM_ID = 76561201960265729;
constexpr quint64 MEATBALL_STEAM_ID = ARTIE_STEAM_ID + 11;
constexpr quint64 FOXY_STEAM_ID = ARTIE_STEAM_ID + 12;
constexpr qint64 FAR_FUTURE_SECONDS = 4102444800;
constexpr QLatin1StringView LIST_PATH{"mobileconf/getlist"};
constexpr int LIST_RESPONSE_COUNT = 6;
// Avatars come from the Steam CDN.
constexpr std::chrono::milliseconds AVATAR_WAIT{2500};

SteamForge::AuthenticatorRecord DemoRecord(const QString& accountName, quint64 steamId, char secretByte) {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source = SteamForge::RecordSource{.origin = SteamForge::RecordOrigin::MA_FILE, .fileName = accountName + QStringLiteral(".maFile"), .importedAt = record.createdAt};
	record.account.accountName = accountName;
	record.account.steamId = steamId;
	record.secrets.sharedSecret = QByteArray(20, secretByte);
	record.secrets.identitySecret = QByteArray(20, static_cast<char>(secretByte + 1));
	record.secrets.deviceId = QStringLiteral("android:00000000-0000-0000-0000-0000000000%1").arg(static_cast<int>(secretByte), 2, 16, QLatin1Char('0'));
	return record;
}

QPushButton* FindStatusChip(const QWidget& window, const char* sourceText) {
	const QString text = QCoreApplication::translate("SteamForge::MainWindow", sourceText);
	const QList<QPushButton*> buttons = window.findChildren<QPushButton*>();
	const auto found = std::ranges::find_if(buttons, [&text](const QPushButton* button) {
		return button->text() == text;
	});
	return found == buttons.end() ? nullptr : *found;
}

// A question runs its own event loop: it is saved and closed from a timer inside that loop.
void SaveModalLater(const QString& filePath, bool& isSaved) {
	QTimer::singleShot(500, [filePath, &isSaved]() {
		QWidget* modalWidget = QApplication::activeModalWidget();
		if (modalWidget == nullptr) {
			return;
		}
		isSaved = modalWidget->grab().save(filePath);
		if (auto* dialog = qobject_cast<QDialog*>(modalWidget); dialog != nullptr) {
			dialog->reject();
		}
	});
}
} // namespace

class CloudSyncRenderTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void initTestCase() { // NOLINT(readability-identifier-naming): QtTest calls this slot by its name.
		QApplication::setWindowIcon(SteamForge::ApplicationIcon());
	}

	void RenderGoogleSync() {
		const QString renderDirectory = qEnvironmentVariable("STEAMFORGE_RENDER_DIRECTORY");
		if (renderDirectory.isEmpty()) {
			QSKIP("Set STEAMFORGE_RENDER_DIRECTORY to render Google sync.");
		}
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);
		SteamForge::InstallWindowFrameStyling(application);
		SteamForge::InterfaceTranslator translator;
		QVERIFY(translator.Apply(RenderLanguage()));
		const QDir output(renderDirectory);

		FakeDrive drive;
		CloudComputer otherComputer(drive);
		CloudComputer thisComputer(drive);
		const SteamForge::AuthenticatorRecord synced = DemoRecord(QStringLiteral("artie2008"), SYNCED_STEAM_ID, 0x31);
		const SteamForge::AuthenticatorRecord conflictInCloud = DemoRecord(QStringLiteral("bonnie_k"), CONFLICT_STEAM_ID, 0x33);
		const SteamForge::AuthenticatorRecord away = DemoRecord(QStringLiteral("mister_kite"), AWAY_STEAM_ID, 0x35);
		for (const SteamForge::AuthenticatorRecord& record : {synced, conflictInCloud, away}) {
			QVERIFY(otherComputer.library->Add(record).has_value());
		}
		const QString recoveryKey = ConnectToGoogle(otherComputer);
		QVERIFY(!recoveryKey.isEmpty());
		for (const SteamForge::AuthenticatorRecord& record : {synced, conflictInCloud, away}) {
			QVERIFY(AddAndWait(otherComputer, record.id));
		}

		SteamForge::AuthenticatorRecord syncedHere = synced;
		syncedHere.id = QUuid::createUuid();
		const SteamForge::AuthenticatorRecord localOnly = DemoRecord(QStringLiteral("dmitry_v"), LOCAL_STEAM_ID, 0x37);
		const SteamForge::AuthenticatorRecord conflictHere = DemoRecord(QStringLiteral("bonnie_k"), CONFLICT_STEAM_ID, 0x39);
		for (const SteamForge::AuthenticatorRecord& record : {syncedHere, localOnly, conflictHere}) {
			QVERIFY(thisComputer.library->Add(record).has_value());
		}
		QVERIFY(!ConnectToGoogle(thisComputer, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*thisComputer.sync));
		const SteamForge::AuthenticatorRecord* awayHere = thisComputer.RecordOf(AWAY_STEAM_ID);
		QVERIFY(awayHere != nullptr);
		const QUuid awayId = awayHere->id;
		QVERIFY(thisComputer.library->Remove(awayId).has_value());
		thisComputer.sync->LeaveInCloud(awayId, QStringLiteral("mister_kite"));
		QVERIFY(SyncAndWait(*thisComputer.sync));
		QCOMPARE(thisComputer.sync->Status(), SteamForge::CloudSyncStatus::NEEDS_DECISION);

		FakeWebApiTransport webApiTransport;
		FakeCommunityTransport communityTransport;
		SteamForge::SteamClock clock;
		SteamForge::SteamImageCache imageCache;
		SteamForge::ConfirmationMonitor monitor(webApiTransport, communityTransport, *thisComputer.library, clock);
		const QDir dataRoot(thisComputer.directory.path());
		SteamForge::ApplicationSettings settings(dataRoot.filePath(QStringLiteral("settings.ini")));
		FakeCommunityTransport profileTransport;
		SteamForge::AccountProfiles profiles(profileTransport, imageCache, dataRoot.filePath(QStringLiteral("profiles.json")));
		SteamForge::MainWindow mainWindow(*thisComputer.library, clock, monitor, imageCache, profiles, *thisComputer.encryption, *thisComputer.connection, *thisComputer.sync, settings, SteamForge::DataLocation{.directoryPath = dataRoot.path(), .isPortable = false});
		mainWindow.show();
		auto* accountList = mainWindow.findChild<SteamForge::AccountListView*>();
		auto* detailsView = mainWindow.findChild<SteamForge::AccountDetailsView*>();
		QVERIFY(accountList != nullptr && detailsView != nullptr);
		accountList->SelectRecord(syncedHere.id);
		QTest::qWait(400);
		QVERIFY(mainWindow.grab().save(output.filePath(QStringLiteral("sync_in_google.png"))));
		accountList->SelectRecord(conflictHere.id);
		QTest::qWait(200);
		QVERIFY(mainWindow.grab().save(output.filePath(QStringLiteral("sync_conflict.png"))));
		accountList->SelectRecord(localOnly.id);
		QTest::qWait(200);
		QVERIFY(mainWindow.grab().save(output.filePath(QStringLiteral("sync_local_only.png"))));

		const SteamForge::SettingsDialog::CloudAccountsSource cloudAccounts{.sync = *thisComputer.sync, .library = *thisComputer.library, .titleProvider = SteamForge::RecordDisplayName};
		SteamForge::SettingsDialog settingsDialog(std::nullopt, true, SteamForge::DataLocation{.directoryPath = dataRoot.path(), .isPortable = false}, *thisComputer.encryption, *thisComputer.connection, cloudAccounts, SteamForge::SettingsSection::GOOGLE);
		settingsDialog.show();
		QTest::qWait(300);
		QVERIFY(settingsDialog.grab().save(output.filePath(QStringLiteral("sync_settings.png"))));
		settingsDialog.hide();

		const SteamForge::AuthenticatorRecord* conflictRecord = thisComputer.library->FindRecord(conflictHere.id);
		QVERIFY(conflictRecord != nullptr);
		SteamForge::CloudConflictDialog conflictDialog(thisComputer.sync->Conflicts().front(), *conflictRecord, SteamForge::RecordDisplayName(*conflictRecord), clock);
		conflictDialog.show();
		QTest::qWait(300);
		QVERIFY(conflictDialog.grab().save(output.filePath(QStringLiteral("sync_choose_version.png"))));
		conflictDialog.hide();

		bool isRemovalSaved = false;
		SaveModalLater(output.filePath(QStringLiteral("sync_remove_account.png")), isRemovalSaved);
		Q_EMIT detailsView->RemoveRequested(syncedHere.id);
		QTRY_VERIFY(isRemovalSaved);
		bool isCloudRemovalSaved = false;
		SaveModalLater(output.filePath(QStringLiteral("sync_remove_from_google.png")), isCloudRemovalSaved);
		Q_EMIT detailsView->RemoveFromGoogleRequested(syncedHere.id);
		QTRY_VERIFY(isCloudRemovalSaved);
		// Both questions were closed without an answer: nothing left the computer or Google.
		QVERIFY(thisComputer.library->FindRecord(syncedHere.id) != nullptr);
		QVERIFY(thisComputer.sync->IsInCloud(syncedHere.id));

		SteamForge::SuccessDialog movedNotice(QCoreApplication::translate("SteamForge::LoginDialog", "Steam Guard moved"), QStringLiteral("Steam Guard of dmitry_v is now in SteamForge."));
		movedNotice.SetKeptValue(QCoreApplication::translate("SteamForge::LoginDialog", "New recovery code"), QStringLiteral("R12345"), QString());
		movedNotice.SetChoice(QCoreApplication::translate("SteamForge::LoginDialog", "Add to Google, so the account is on your other computers too"), true);
		movedNotice.show();
		SteamForge::ImportDialog importDialog(*thisComputer.library);
		importDialog.OfferGoogle();
		importDialog.show();
		QTest::qWait(300);
		QVERIFY(movedNotice.grab().save(output.filePath(QStringLiteral("sync_moved_guard.png"))));
		QVERIFY(importDialog.grab().save(output.filePath(QStringLiteral("sync_import.png"))));
	}

	// The main window, the Google section of the settings and the choice between two Steam Guards, with the players
	// of the other frames of the README.
	void RenderReadmeFrames() {
		const QString renderDirectory = qEnvironmentVariable("STEAMFORGE_RENDER_DIRECTORY");
		if (renderDirectory.isEmpty()) {
			QSKIP("Set STEAMFORGE_RENDER_DIRECTORY to render the frames of the README.");
		}
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);
		SteamForge::InstallWindowFrameStyling(application);
		SteamForge::InterfaceTranslator translator;
		const SteamForge::InterfaceLanguage language = RenderLanguage();
		QVERIFY(translator.Apply(language));
		const QString suffix = language == SteamForge::InterfaceLanguage::ENGLISH ? QStringLiteral("en") : QStringLiteral("ru");
		const QDir output(renderDirectory);

		const std::array demoAccounts{
			DemoAccount{.steamId = ARTIE_STEAM_ID, .accountName = QStringLiteral("artie2008"), .nickname = QStringLiteral("Artie"), .avatarHash = QLatin1StringView("4264b561c93fcd0d4c832c1137229587bbb5f8d7")},
			DemoAccount{.steamId = MEATBALL_STEAM_ID, .accountName = QStringLiteral("meatball_cs"), .nickname = QStringLiteral("meatball"), .avatarHash = QLatin1StringView("2db6e5f11d2eb0ff1410cd6353f771da213124a0")},
			DemoAccount{.steamId = FOXY_STEAM_ID, .accountName = QStringLiteral("foxy_plays"), .nickname = QStringLiteral("Foxy"), .avatarHash = QLatin1StringView("95cc644e231f05cf0590311be14b2924ed66f66b")},
		};
		SteamForge::AuthenticatorRecord artie = DemoRecord(QStringLiteral("artie2008"), ARTIE_STEAM_ID, 0x21);
		artie.secrets.revocationCode = QStringLiteral("R12345");
		const SteamForge::AuthenticatorRecord meatball = DemoRecord(QStringLiteral("meatball_cs"), MEATBALL_STEAM_ID, 0x23);
		const SteamForge::AuthenticatorRecord kite = DemoRecord(QStringLiteral("mister_kite"), AWAY_STEAM_ID, 0x25);
		const SteamForge::AuthenticatorRecord foxy = DemoRecord(QStringLiteral("foxy_plays"), FOXY_STEAM_ID, 0x27);
		SteamForge::AuthenticatorRecord smurf;
		smurf.id = QUuid::createUuid();
		smurf.label = QStringLiteral("smurf");
		smurf.createdAt = QDateTime::currentDateTimeUtc();
		smurf.source.origin = SteamForge::RecordOrigin::OTPAUTH_URI;
		smurf.source.importedAt = smurf.createdAt;
		smurf.secrets.sharedSecret = QByteArray(20, 0x29);

		// Artie and meatball are in Google, smurf and Foxy only here, and the kite account was removed here but stays in Google.
		FakeDrive drive;
		CloudComputer otherComputer(drive);
		CloudComputer thisComputer(drive);
		for (const SteamForge::AuthenticatorRecord& record : {artie, meatball, kite}) {
			QVERIFY(otherComputer.library->Add(record).has_value());
		}
		const QString recoveryKey = ConnectToGoogle(otherComputer);
		QVERIFY(!recoveryKey.isEmpty());
		for (const SteamForge::AuthenticatorRecord& record : {artie, meatball, kite}) {
			QVERIFY(AddAndWait(otherComputer, record.id));
		}
		SteamForge::AuthenticatorRecord artieHere = artie;
		artieHere.id = QUuid::createUuid();
		QVERIFY(thisComputer.library->Add(artieHere).has_value());
		QVERIFY(thisComputer.library->SaveSession({.recordId = artieHere.id, .steamId = ARTIE_STEAM_ID, .accountName = QStringLiteral("artie2008"), .refreshToken = SyntheticJwt(ARTIE_STEAM_ID, {QStringLiteral("derive")}, FAR_FUTURE_SECONDS), .accessToken = SyntheticJwt(ARTIE_STEAM_ID, {QStringLiteral("mobile")}, FAR_FUTURE_SECONDS), .signedInAt = QDateTime::currentDateTimeUtc()}).has_value());
		QVERIFY(thisComputer.library->Add(smurf).has_value());
		QVERIFY(!ConnectToGoogle(thisComputer, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*thisComputer.sync));
		const SteamForge::AuthenticatorRecord* kiteHere = thisComputer.RecordOf(AWAY_STEAM_ID);
		QVERIFY(kiteHere != nullptr);
		const QUuid kiteId = kiteHere->id;
		QVERIFY(thisComputer.library->Remove(kiteId).has_value());
		thisComputer.sync->LeaveInCloud(kiteId, QStringLiteral("Kite"));
		QVERIFY(thisComputer.library->Add(foxy).has_value());
		QVERIFY(SyncAndWait(*thisComputer.sync));
		QCOMPARE(thisComputer.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);

		FakeWebApiTransport webApiTransport;
		FakeCommunityTransport communityTransport;
		for (int i = 0; i < LIST_RESPONSE_COUNT; ++i) {
			communityTransport.AddJson(LIST_PATH, QJsonObject{{QStringLiteral("success"), true}, {QStringLiteral("conf"), QJsonArray()}});
		}
		const QJsonObject eligibility{{QStringLiteral("allowed"), 1}};
		communityTransport.AddCookies(QStringLiteral("market/eligibilitycheck/"), QList<QNetworkCookie>{QNetworkCookie(QByteArray("webTradeEligibility"), QJsonDocument(eligibility).toJson(QJsonDocument::Compact).toPercentEncoding())});
		SteamForge::SteamClock clock;
		SteamForge::SteamImageCache imageCache;
		SteamForge::ConfirmationMonitor monitor(webApiTransport, communityTransport, *thisComputer.library, clock);
		const QDir dataRoot(thisComputer.directory.path());
		SteamForge::ApplicationSettings settings(dataRoot.filePath(QStringLiteral("settings.ini")));
		FakeCommunityTransport profileTransport;
		for (const DemoAccount& account : demoAccounts) {
			profileTransport.AddResponse(SteamForge::CommunityProfilePath(account.steamId), DemoProfileXml(account));
		}
		SteamForge::AccountProfiles profiles(profileTransport, imageCache, dataRoot.filePath(QStringLiteral("profiles.json")));
		SteamForge::MainWindow mainWindow(*thisComputer.library, clock, monitor, imageCache, profiles, *thisComputer.encryption, *thisComputer.connection, *thisComputer.sync, settings, SteamForge::DataLocation{.directoryPath = dataRoot.path(), .isPortable = false});
		monitor.Start();
		SteamForge::AccountConfirmations* artieConfirmations = monitor.Find(artieHere.id);
		QVERIFY(artieConfirmations != nullptr);
		artieConfirmations->Refresh();
		mainWindow.show();
		auto* accountList = mainWindow.findChild<SteamForge::AccountListView*>();
		QVERIFY(accountList != nullptr);
		accountList->SelectRecord(artieHere.id);
		QTest::qWait(AVATAR_WAIT);
		QTRY_COMPARE(thisComputer.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QVERIFY(mainWindow.grab().save(output.filePath(QStringLiteral("accounts-%1.png").arg(suffix))));

		// The status at the top opens the Google section.
		QPushButton* syncedChip = FindStatusChip(mainWindow, "Synced");
		QVERIFY(syncedChip != nullptr);
		bool isSettingsSaved = false;
		SaveModalLater(output.filePath(QStringLiteral("google-%1.png").arg(suffix)), isSettingsSaved);
		syncedChip->click();
		QTRY_VERIFY(isSettingsSaved);

		// Another computer puts a different Steam Guard of Foxy into Google; the status asks to choose.
		const SteamForge::AuthenticatorRecord otherFoxy = DemoRecord(QStringLiteral("foxy_plays"), FOXY_STEAM_ID, 0x2b);
		QVERIFY(otherComputer.library->Add(otherFoxy).has_value());
		QVERIFY(AddAndWait(otherComputer, otherFoxy.id));
		QVERIFY(SyncAndWait(*thisComputer.sync));
		QCOMPARE(thisComputer.sync->Status(), SteamForge::CloudSyncStatus::NEEDS_DECISION);
		QPushButton* chooseChip = FindStatusChip(mainWindow, "Choose a version");
		QVERIFY(chooseChip != nullptr);
		bool isConflictSaved = false;
		SaveModalLater(output.filePath(QStringLiteral("google-conflict-%1.png").arg(suffix)), isConflictSaved);
		chooseChip->click();
		QTRY_VERIFY(isConflictSaved);
		QVERIFY(thisComputer.library->FindRecord(foxy.id) != nullptr);
	}
};

QTEST_MAIN(CloudSyncRenderTest)

#include "cloud_sync_render_test.moc"
