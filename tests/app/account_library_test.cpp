#include "app/account_library.h"

namespace {
constexpr quint64 FIRST_STEAM_ID = 76561198000000001;
constexpr quint64 SECOND_STEAM_ID = 76561198000000002;

SteamForge::AuthenticatorRecord MakeRecord(char secretSeed, std::optional<quint64> steamId, const QString& accountName) {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source.origin = SteamForge::RecordOrigin::MA_FILE;
	record.source.importedAt = record.createdAt;
	record.account.steamId = steamId;
	record.account.accountName = accountName;
	record.secrets.sharedSecret = QByteArray(20, secretSeed);
	return record;
}

SteamForge::AuthenticatorStore StoreIn(const QTemporaryDir& temporaryDirectory) {
	return SteamForge::AuthenticatorStore(temporaryDirectory.filePath(QStringLiteral("accounts")));
}

SteamForge::SessionStore SessionsIn(const QTemporaryDir& temporaryDirectory) {
	return SteamForge::SessionStore(temporaryDirectory.filePath(QStringLiteral("sessions")));
}

SteamForge::AccountSession MakeSession(const SteamForge::AuthenticatorRecord& record) {
	return {
		.recordId = record.id,
		.steamId = record.account.steamId.value_or(0),
		.accountName = record.account.accountName,
		.refreshToken = QStringLiteral("synthetic.refresh.token"),
		.accessToken = QStringLiteral("synthetic.access.token"),
		.signedInAt = QDateTime::currentDateTimeUtc(),
	};
}
} // namespace

class AccountLibraryTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void AddedRecordsSurviveRestart() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary accountLibrary(StoreIn(temporaryDirectory), SessionsIn(temporaryDirectory));
		accountLibrary.Load();
		const QSignalSpy changeSpy(&accountLibrary, &SteamForge::AccountLibrary::RecordsChanged);
		const SteamForge::AuthenticatorRecord record = MakeRecord('a', FIRST_STEAM_ID, QStringLiteral("first"));
		QVERIFY(accountLibrary.Add(record).has_value());
		QCOMPARE(changeSpy.count(), 1);

		SteamForge::AccountLibrary restartedLibrary(StoreIn(temporaryDirectory), SessionsIn(temporaryDirectory));
		restartedLibrary.Load();
		QCOMPARE(restartedLibrary.Records().size(), std::size_t{1});
		QVERIFY(restartedLibrary.FindRecord(record.id) != nullptr);
	}

	void MatchesBySecretsBeforeAccountIdentity() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary accountLibrary(StoreIn(temporaryDirectory), SessionsIn(temporaryDirectory));
		accountLibrary.Load();
		QVERIFY(accountLibrary.Add(MakeRecord('a', FIRST_STEAM_ID, QStringLiteral("first"))).has_value());

		QCOMPARE(accountLibrary.MatchExisting(MakeRecord('a', std::nullopt, QString())), SteamForge::ImportMatch::SAME_AUTHENTICATOR);
		QCOMPARE(accountLibrary.MatchExisting(MakeRecord('b', FIRST_STEAM_ID, QStringLiteral("renamed"))), SteamForge::ImportMatch::OTHER_AUTHENTICATOR_FOR_ACCOUNT);
		QCOMPARE(accountLibrary.MatchExisting(MakeRecord('b', std::nullopt, QStringLiteral("FIRST"))), SteamForge::ImportMatch::OTHER_AUTHENTICATOR_FOR_ACCOUNT);
		QCOMPARE(accountLibrary.MatchExisting(MakeRecord('b', SECOND_STEAM_ID, QStringLiteral("first"))), SteamForge::ImportMatch::NEW_ACCOUNT);
		QCOMPARE(accountLibrary.MatchExisting(MakeRecord('c', std::nullopt, QString())), SteamForge::ImportMatch::NEW_ACCOUNT);
	}

	void RecordsOfOneSteamIdStaySeparate() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary accountLibrary(StoreIn(temporaryDirectory), SessionsIn(temporaryDirectory));
		accountLibrary.Load();
		const SteamForge::AuthenticatorRecord olderAuthenticator = MakeRecord('a', FIRST_STEAM_ID, QStringLiteral("first"));
		const SteamForge::AuthenticatorRecord newerAuthenticator = MakeRecord('b', FIRST_STEAM_ID, QStringLiteral("first"));
		QVERIFY(accountLibrary.Add(olderAuthenticator).has_value());
		QVERIFY(accountLibrary.Add(newerAuthenticator).has_value());

		SteamForge::AccountLibrary restartedLibrary(StoreIn(temporaryDirectory), SessionsIn(temporaryDirectory));
		restartedLibrary.Load();
		QCOMPARE(restartedLibrary.Records().size(), std::size_t{2});
		QCOMPARE(restartedLibrary.FindRecord(olderAuthenticator.id)->secrets.sharedSecret, olderAuthenticator.secrets.sharedSecret);
		QCOMPARE(restartedLibrary.FindRecord(newerAuthenticator.id)->secrets.sharedSecret, newerAuthenticator.secrets.sharedSecret);
	}

	void RemoveAffectsOnlyTheChosenRecord() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary accountLibrary(StoreIn(temporaryDirectory), SessionsIn(temporaryDirectory));
		accountLibrary.Load();
		const SteamForge::AuthenticatorRecord keptRecord = MakeRecord('a', FIRST_STEAM_ID, QStringLiteral("first"));
		const SteamForge::AuthenticatorRecord removedRecord = MakeRecord('b', FIRST_STEAM_ID, QStringLiteral("first"));
		QVERIFY(accountLibrary.Add(keptRecord).has_value());
		QVERIFY(accountLibrary.Add(removedRecord).has_value());

		QVERIFY(accountLibrary.Remove(removedRecord.id).has_value());
		QCOMPARE(accountLibrary.Records().size(), std::size_t{1});
		QVERIFY(accountLibrary.FindRecord(keptRecord.id) != nullptr);
		QVERIFY(accountLibrary.FindRecord(removedRecord.id) == nullptr);
	}

	void SessionsSurviveRestartAndLeaveWithTheirRecord() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AccountLibrary accountLibrary(StoreIn(temporaryDirectory), SessionsIn(temporaryDirectory));
		accountLibrary.Load();
		const SteamForge::AuthenticatorRecord signedInRecord = MakeRecord('a', FIRST_STEAM_ID, QStringLiteral("first"));
		const SteamForge::AuthenticatorRecord otherRecord = MakeRecord('b', SECOND_STEAM_ID, QStringLiteral("second"));
		QVERIFY(accountLibrary.Add(signedInRecord).has_value());
		QVERIFY(accountLibrary.Add(otherRecord).has_value());
		const QSignalSpy sessionSpy(&accountLibrary, &SteamForge::AccountLibrary::SessionChanged);
		QVERIFY(accountLibrary.SaveSession(MakeSession(signedInRecord)).has_value());
		QCOMPARE(sessionSpy.count(), 1);

		SteamForge::AccountLibrary restartedLibrary(StoreIn(temporaryDirectory), SessionsIn(temporaryDirectory));
		restartedLibrary.Load();
		const std::optional<SteamForge::AccountSession> restoredSession = restartedLibrary.FindSession(signedInRecord.id);
		QVERIFY(restoredSession.has_value());
		QCOMPARE(restoredSession->steamId, FIRST_STEAM_ID);
		QCOMPARE(restoredSession->refreshToken, QStringLiteral("synthetic.refresh.token"));
		QVERIFY(!restartedLibrary.FindSession(otherRecord.id).has_value());

		QVERIFY(restartedLibrary.Remove(signedInRecord.id).has_value());
		QVERIFY(!restartedLibrary.FindSession(signedInRecord.id).has_value());
		QVERIFY(!QFileInfo::exists(QDir(temporaryDirectory.filePath(QStringLiteral("sessions"))).filePath(signedInRecord.id.toString(QUuid::WithoutBraces) + QStringLiteral(".json"))));
	}

	void FailedSaveLeavesTheLibraryUnchanged() {
		const QTemporaryDir temporaryDirectory;
		const QString blockingFilePath = temporaryDirectory.filePath(QStringLiteral("blocked"));
		QFile blockingFile(blockingFilePath);
		QVERIFY(blockingFile.open(QIODevice::WriteOnly));
		blockingFile.close();

		SteamForge::AccountLibrary accountLibrary(SteamForge::AuthenticatorStore(QDir(blockingFilePath).filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(QDir(blockingFilePath).filePath(QStringLiteral("sessions"))));
		accountLibrary.Load();
		const QSignalSpy changeSpy(&accountLibrary, &SteamForge::AccountLibrary::RecordsChanged);
		QVERIFY(!accountLibrary.Add(MakeRecord('a', FIRST_STEAM_ID, QStringLiteral("first"))).has_value());
		QVERIFY(accountLibrary.Records().empty());
		QCOMPARE(changeSpy.count(), 0);
	}
};

QTEST_GUILESS_MAIN(AccountLibraryTest)

#include "account_library_test.moc"
