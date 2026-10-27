#include "app/account_profiles.h"
#include "app/application_settings.h"
#include "app/confirmation_monitor.h"
#include "platform/application_icons.h"
#include "platform/data_location.h"
#include "platform/window_frame.h"
#include "steam/steam_clock.h"
#include "steam/steam_image_cache.h"
#include "support/cloud_computer.h"
#include "support/fake_community_transport.h"
#include "support/fake_web_api_transport.h"
#include "support/render_language.h"
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
};

QTEST_MAIN(CloudSyncRenderTest)

#include "cloud_sync_render_test.moc"
