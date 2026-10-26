#include "app/application_settings.h"

class ApplicationSettingsTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ChecksForUpdatesUntilTurnedOff() {
		const QTemporaryDir dataDirectory;
		QVERIFY(dataDirectory.isValid());
		const QString filePath = dataDirectory.filePath(QStringLiteral("settings.ini"));
		{
			SteamForge::ApplicationSettings settings(filePath);
			QVERIFY(settings.ChecksForUpdates());
			settings.SetChecksForUpdates(false);
		}
		const SteamForge::ApplicationSettings reopenedSettings(filePath);
		QVERIFY(!reopenedSettings.ChecksForUpdates());
	}

	void RemembersTheSkippedVersion() {
		const QTemporaryDir dataDirectory;
		QVERIFY(dataDirectory.isValid());
		const QString filePath = dataDirectory.filePath(QStringLiteral("settings.ini"));
		{
			SteamForge::ApplicationSettings settings(filePath);
			QVERIFY(settings.SkippedUpdateVersion().isNull());
			settings.SetSkippedUpdateVersion(QVersionNumber(0, 2, 0));
		}
		const SteamForge::ApplicationSettings reopenedSettings(filePath);
		QCOMPARE(reopenedSettings.SkippedUpdateVersion(), QVersionNumber(0, 2, 0));
	}
};

QTEST_GUILESS_MAIN(ApplicationSettingsTest)

#include "application_settings_test.moc"
