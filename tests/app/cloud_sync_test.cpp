#include "support/cloud_computer.h"

namespace {

constexpr quint64 FIRST_STEAM_ID = 76561198000000001;
constexpr quint64 SECOND_STEAM_ID = 76561198000000002;

SteamForge::AuthenticatorRecord MakeRecord(const QString& accountName, quint64 steamId, char secretByte) {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source = SteamForge::RecordSource{.origin = SteamForge::RecordOrigin::MA_FILE, .fileName = accountName + QStringLiteral(".maFile"), .importedAt = record.createdAt};
	record.account.accountName = accountName;
	record.account.steamId = steamId;
	record.secrets.sharedSecret = QByteArray(20, secretByte);
	return record;
}

// A folder under the name of the file: the next write fails there, as on a broken or full disk.
bool BreakWritesAt(const QString& filePath) {
	QFile::remove(filePath);
	return QDir().mkpath(filePath);
}

QString RecordFileOf(const CloudComputer& computer, const QUuid& recordId) {
	return computer.directory.filePath(QStringLiteral("accounts/%1.json").arg(recordId.toString(QUuid::WithoutBraces)));
}

// The account changed on both computers since the last sync: the second one asks which version to keep.
void ChangeOnBothComputers(CloudComputer& first, CloudComputer& second, SteamForge::AuthenticatorRecord& account) {
	QVERIFY(first.library->Add(account).has_value());
	const QString recoveryKey = ConnectToGoogle(first);
	QVERIFY(AddAndWait(first, account.id));
	QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
	QVERIFY(SyncAndWait(*second.sync));

	account.label = QStringLiteral("changed on the first computer");
	QVERIFY(first.library->Save(account).has_value());
	QVERIFY(SyncAndWait(*first.sync));
	const SteamForge::AuthenticatorRecord* arrived = second.RecordOf(FIRST_STEAM_ID);
	QVERIFY(arrived != nullptr);
	SteamForge::AuthenticatorRecord secondCopy = *arrived;
	secondCopy.label = QStringLiteral("changed on the second computer");
	QVERIFY(second.library->Save(secondCopy).has_value());
	QVERIFY(SyncAndWait(*second.sync));
	QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::NEEDS_DECISION);
	QCOMPARE(second.sync->Conflicts().front().kind, SteamForge::CloudConflictKind::CHANGED_ON_BOTH);
}

void WaitForTheDecision(const CloudComputer& computer, const QUuid& recordId) {
	QTRY_VERIFY(!computer.sync->IsChanging(recordId));
	QTRY_VERIFY(computer.sync->Status() != SteamForge::CloudSyncStatus::SYNCING);
}

} // namespace

class CloudSyncTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void BringsTheCloudAccountsToAnotherComputer() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(!recoveryKey.isEmpty());
		QVERIFY(AddAndWait(first, account.id));

		QVERIFY(second.library->Add(MakeRecord(QStringLiteral("local_only"), SECOND_STEAM_ID, 0x42)).has_value());
		const QSignalSpy addedSpy(second.sync.get(), &SteamForge::CloudSync::RecordAdded);
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));

		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QCOMPARE(second.library->Records().size(), std::size_t{2});
		const SteamForge::AuthenticatorRecord* arrived = second.RecordOf(FIRST_STEAM_ID);
		QVERIFY(arrived != nullptr);
		QCOMPARE(arrived->secrets.sharedSecret, account.secrets.sharedSecret);
		QVERIFY(second.sync->IsInCloud(arrived->id));
		QCOMPARE(addedSpy.count(), 1);
		// The account that was only on the second computer stays there, out of the cloud.
		QVERIFY(!second.sync->IsInCloud(second.RecordOf(SECOND_STEAM_ID)->id));
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
	}

	void LinksTheSameSteamGuardWithoutAsking() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));

		SteamForge::AuthenticatorRecord sameGuard = account;
		sameGuard.id = QUuid::createUuid();
		sameGuard.secrets.revocationCode = QStringLiteral("R12345");
		QVERIFY(second.library->Add(sameGuard).has_value());
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));
		// What the second computer adds goes up on the next pass.
		QVERIFY(SyncAndWait(*second.sync));

		QCOMPARE(second.library->Records().size(), std::size_t{1});
		QVERIFY(second.sync->IsInCloud(sameGuard.id));
		QVERIFY(second.sync->Conflicts().empty());
		QVERIFY(SyncAndWait(*first.sync));
		QCOMPARE(first.library->FindRecord(account.id)->secrets.revocationCode, QStringLiteral("R12345"));
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
	}

	void WaitsForADecisionOnAnotherKeyOfAnAccount() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord cloudGuard = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(cloudGuard).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, cloudGuard.id));

		const SteamForge::AuthenticatorRecord localGuard = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x51);
		QVERIFY(second.library->Add(localGuard).has_value());
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));

		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::NEEDS_DECISION);
		QCOMPARE(second.sync->Conflicts().size(), std::size_t{1});
		QCOMPARE(second.sync->Conflicts().front().kind, SteamForge::CloudConflictKind::OTHER_KEY_OF_ACCOUNT);
		QVERIFY(second.sync->HasConflict(localGuard.id));
		// Nothing is merged or overwritten before the decision.
		QCOMPARE(second.library->Records().size(), std::size_t{1});
		QCOMPARE(second.library->FindRecord(localGuard.id)->secrets.sharedSecret, localGuard.secrets.sharedSecret);

		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, SteamForge::ConflictChoice::KEEP_BOTH);
		QCOMPARE(second.library->Records().size(), std::size_t{2});
		QVERIFY(!second.sync->IsInCloud(localGuard.id));
		QVERIFY(SyncAndWait(*second.sync));
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
	}

	void ReplacesTheLocalKeyOnlyAfterABackup() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord cloudGuard = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(cloudGuard).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, cloudGuard.id));
		const SteamForge::AuthenticatorRecord localGuard = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x51);
		QVERIFY(second.library->Add(localGuard).has_value());
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));
		QCOMPARE(second.sync->Conflicts().size(), std::size_t{1});

		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, SteamForge::ConflictChoice::USE_CLOUD);
		QTRY_VERIFY(second.sync->IsInCloud(localGuard.id));
		QTRY_COMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QCOMPARE(second.library->FindRecord(localGuard.id)->secrets.sharedSecret, cloudGuard.secrets.sharedSecret);
		QCOMPARE(second.BackupCount(), 1);
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
	}

	void KeepsADecisionMadeDuringAPass() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord cloudGuard = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(cloudGuard).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, cloudGuard.id));
		const SteamForge::AuthenticatorRecord localGuard = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x51);
		QVERIFY(second.library->Add(localGuard).has_value());
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));
		QCOMPARE(second.sync->Conflicts().size(), std::size_t{1});

		second.sync->Synchronize();
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCING);
		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, SteamForge::ConflictChoice::USE_CLOUD);
		QVERIFY(second.sync->IsChanging(localGuard.id));
		QTRY_VERIFY(!second.sync->IsChanging(localGuard.id));
		QTRY_COMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);

		QVERIFY(second.sync->Conflicts().empty());
		QVERIFY(second.sync->IsInCloud(localGuard.id));
		QCOMPARE(second.library->FindRecord(localGuard.id)->secrets.sharedSecret, cloudGuard.secrets.sharedSecret);
		QCOMPARE(second.BackupCount(), 1);
	}

	void KeepsTheReplacedKeyOnTheOtherComputer() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord cloudGuard = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(cloudGuard).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, cloudGuard.id));
		const SteamForge::AuthenticatorRecord localGuard = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x51);
		QVERIFY(second.library->Add(localGuard).has_value());
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));

		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, SteamForge::ConflictChoice::USE_LOCAL);
		QTRY_COMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		const QSignalSpy replacedSpy(first.sync.get(), &SteamForge::CloudSync::KeyReplaced);
		QVERIFY(SyncAndWait(*first.sync));

		QCOMPARE(first.library->FindRecord(cloudGuard.id)->secrets.sharedSecret, localGuard.secrets.sharedSecret);
		QCOMPARE(first.BackupCount(), 1);
		QCOMPARE(replacedSpy.count(), 1);
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
	}

	void AddsAnAccountOnceWithoutShowingItToGoogle() {
		FakeDrive drive;
		CloudComputer computer(drive);
		const SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(computer.library->Add(account).has_value());
		QVERIFY(!ConnectToGoogle(computer).isEmpty());
		QVERIFY(SyncAndWait(*computer.sync));

		const QSignalSpy finishedSpy(computer.sync.get(), &SteamForge::CloudSync::AddingFinished);
		computer.sync->AddToCloud(account.id);
		computer.sync->AddToCloud(account.id);
		QTRY_COMPARE(finishedSpy.count(), 1);
		QVERIFY(finishedSpy.front().at(1).toBool());
		QTRY_COMPARE(computer.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);

		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
		QVERIFY(!drive.Shows("first_player"));
		QVERIFY(!drive.Shows(QByteArray::number(FIRST_STEAM_ID)));
		QVERIFY(!drive.Shows(account.secrets.sharedSecret.toBase64()));
		const SteamForge::GoogleClient client{.clientId = QStringLiteral("client-id"), .clientSecret = {}};
		SteamForge::GoogleConnection restarted(drive, *computer.encryption, client, FakeDriveEndpoints(), computer.directory.filePath(QStringLiteral("google.json")));
		restarted.Load();
		QTRY_COMPARE(restarted.State(), SteamForge::GoogleConnectionState::CONNECTED);
		QCOMPARE(restarted.Links().size(), std::size_t{1});
		QCOMPARE(restarted.Links().front().localRecordId, account.id);
	}

	void SendsALocalChangeToTheOtherComputer() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));

		account.secrets.identitySecret = QByteArray(20, 0x61);
		QVERIFY(first.library->Save(account).has_value());
		QVERIFY(SyncAndWait(*first.sync));
		QVERIFY(SyncAndWait(*second.sync));

		QCOMPARE(second.RecordOf(FIRST_STEAM_ID)->secrets.identitySecret, account.secrets.identitySecret);
		// The previous revision was removed once the new one was in place.
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
		QVERIFY(second.sync->Conflicts().empty());
	}

	void KeepsAnAccountRemovedFromGoogleOnOtherComputers() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));
		const QUuid secondCopyId = second.RecordOf(FIRST_STEAM_ID)->id;

		first.sync->RemoveFromCloud(account.id);
		QTRY_VERIFY(!first.sync->IsInCloud(account.id));
		const QSignalSpy unlinkedSpy(second.sync.get(), &SteamForge::CloudSync::RecordUnlinked);
		QVERIFY(SyncAndWait(*second.sync));

		QCOMPARE(drive.FileCount(QStringLiteral("record")), 0);
		QVERIFY(first.library->FindRecord(account.id) != nullptr);
		QVERIFY(second.library->FindRecord(secondCopyId) != nullptr);
		QVERIFY(!second.sync->IsInCloud(secondCopyId));
		QCOMPARE(unlinkedSpy.count(), 1);
	}

	void DoesNotBringBackAnAccountRemovedOnlyHere() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));
		const QUuid secondCopyId = second.RecordOf(FIRST_STEAM_ID)->id;

		QVERIFY(second.library->Remove(secondCopyId).has_value());
		second.sync->LeaveInCloud(secondCopyId, QStringLiteral("first_player"));
		QVERIFY(SyncAndWait(*second.sync));
		QVERIFY(second.library->Records().empty());
		QCOMPARE(second.connection->CloudOnlyRecords().size(), std::size_t{1});
		QCOMPARE(second.connection->CloudOnlyRecords().front().title, QStringLiteral("first_player"));
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
		QVERIFY(SyncAndWait(*first.sync));
		QVERIFY(first.sync->IsInCloud(account.id));

		const QSignalSpy addedSpy(second.sync.get(), &SteamForge::CloudSync::RecordAdded);
		second.sync->ReturnFromCloud(second.connection->CloudOnlyRecords().front().cloudRecordId);
		QTRY_COMPARE(addedSpy.count(), 1);
		QTRY_COMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QVERIFY(second.connection->CloudOnlyRecords().empty());
		QCOMPARE(second.RecordOf(FIRST_STEAM_ID)->secrets.sharedSecret, account.secrets.sharedSecret);
	}

	void RemovesAnAccountHereAndFromGoogle() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));
		const QUuid secondCopyId = second.RecordOf(FIRST_STEAM_ID)->id;

		// As the removal question does it: the copy counts as left in Google until it is gone.
		QVERIFY(second.library->Remove(secondCopyId).has_value());
		second.sync->RemoveFromCloud(secondCopyId);
		second.sync->LeaveInCloud(secondCopyId, QStringLiteral("first_player"));
		QTRY_VERIFY(!second.sync->IsChanging(secondCopyId));
		QTRY_COMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);

		QCOMPARE(drive.FileCount(QStringLiteral("record")), 0);
		QVERIFY(second.connection->CloudOnlyRecords().empty());
		QVERIFY(second.connection->Links().empty());
		const QSignalSpy unlinkedSpy(first.sync.get(), &SteamForge::CloudSync::RecordUnlinked);
		QVERIFY(SyncAndWait(*first.sync));
		QCOMPARE(unlinkedSpy.count(), 1);
		QVERIFY(first.library->FindRecord(account.id) != nullptr);
	}

	void AsksWhenBothComputersChangedTheAccount() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));

		account.label = QStringLiteral("changed on the first computer");
		QVERIFY(first.library->Save(account).has_value());
		QVERIFY(SyncAndWait(*first.sync));
		SteamForge::AuthenticatorRecord secondCopy = *second.RecordOf(FIRST_STEAM_ID);
		secondCopy.label = QStringLiteral("changed on the second computer");
		QVERIFY(second.library->Save(secondCopy).has_value());
		QVERIFY(SyncAndWait(*second.sync));

		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::NEEDS_DECISION);
		QCOMPARE(second.sync->Conflicts().front().kind, SteamForge::CloudConflictKind::CHANGED_ON_BOTH);
		QCOMPARE(second.library->FindRecord(secondCopy.id)->label, QStringLiteral("changed on the second computer"));

		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, SteamForge::ConflictChoice::USE_LOCAL);
		QTRY_COMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QVERIFY(SyncAndWait(*first.sync));
		QCOMPARE(first.library->FindRecord(account.id)->label, QStringLiteral("changed on the second computer"));
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
	}

	// The chosen cloud version could not be written here: it stays in Google for every computer.
	void KeepsTheCloudVersionWhenItCannotBeSavedHere() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		ChangeOnBothComputers(first, second, account);
		const SteamForge::AuthenticatorRecord* secondCopy = second.RecordOf(FIRST_STEAM_ID);
		QVERIFY(secondCopy != nullptr);
		const QUuid secondCopyId = secondCopy->id;
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);

		QVERIFY(BreakWritesAt(RecordFileOf(second, secondCopyId)));
		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, SteamForge::ConflictChoice::USE_CLOUD);
		WaitForTheDecision(second, secondCopyId);

		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
		QCOMPARE(second.library->FindRecord(secondCopyId)->label, QStringLiteral("changed on the second computer"));
		// The question stays, and the status says the answer did not take.
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::NOT_SAVED);
		QCOMPARE(second.sync->Conflicts().size(), std::size_t{1});
		QVERIFY(SyncAndWait(*first.sync));
		QVERIFY(first.sync->IsInCloud(account.id));
	}

	void KeepsAChangeMadeWhileTheQuestionWasOpen_data() {
		QTest::addColumn<SteamForge::ConflictChoice>("choice");
		QTest::newRow("cloud version") << SteamForge::ConflictChoice::USE_CLOUD;
		QTest::newRow("this computer's version") << SteamForge::ConflictChoice::USE_LOCAL;
	}

	// The first computer changed the account again before the second one answered: the answer was about a version
	// that is gone, so it is asked again with the newest one, and nothing of the first computer is lost.
	void KeepsAChangeMadeWhileTheQuestionWasOpen() {
		QFETCH(SteamForge::ConflictChoice, choice);
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		ChangeOnBothComputers(first, second, account);
		const SteamForge::AuthenticatorRecord* secondCopy = second.RecordOf(FIRST_STEAM_ID);
		QVERIFY(secondCopy != nullptr);
		const QUuid secondCopyId = secondCopy->id;
		account.label = QStringLiteral("changed again on the first computer");
		QVERIFY(first.library->Save(account).has_value());
		QVERIFY(SyncAndWait(*first.sync));

		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, choice);
		WaitForTheDecision(second, secondCopyId);

		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::NEEDS_DECISION);
		QCOMPARE(second.sync->Conflicts().size(), std::size_t{1});
		QCOMPARE(second.sync->Conflicts().front().cloudRecord.label, QStringLiteral("changed again on the first computer"));
		QVERIFY(SyncAndWait(*first.sync));
		QVERIFY(first.sync->IsInCloud(account.id));
		QCOMPARE(first.library->FindRecord(account.id)->label, QStringLiteral("changed again on the first computer"));

		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, SteamForge::ConflictChoice::USE_LOCAL);
		WaitForTheDecision(second, secondCopyId);
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QVERIFY(SyncAndWait(*first.sync));
		QCOMPARE(first.library->FindRecord(account.id)->label, QStringLiteral("changed on the second computer"));
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
	}

	void SettlesTwoEqualRevisions_data() {
		KeepsAChangeMadeWhileTheQuestionWasOpen_data();
	}

	// Both computers sent a change at once, as the same next revision: the answer leaves one version for both.
	void SettlesTwoEqualRevisions() {
		QFETCH(SteamForge::ConflictChoice, choice);
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));
		const SteamForge::AuthenticatorRecord* arrived = second.RecordOf(FIRST_STEAM_ID);
		QVERIFY(arrived != nullptr);
		SteamForge::AuthenticatorRecord secondCopy = *arrived;

		account.label = QStringLiteral("changed on the first computer");
		QVERIFY(first.library->Save(account).has_value());
		secondCopy.label = QStringLiteral("changed on the second computer");
		QVERIFY(second.library->Save(secondCopy).has_value());
		first.sync->Synchronize();
		second.sync->Synchronize();
		QTRY_VERIFY(first.sync->Status() != SteamForge::CloudSyncStatus::SYNCING && second.sync->Status() != SteamForge::CloudSyncStatus::SYNCING);
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 2);
		QVERIFY(SyncAndWait(*second.sync));
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::NEEDS_DECISION);

		second.sync->ResolveConflict(second.sync->Conflicts().front().cloudRecordId, choice);
		WaitForTheDecision(second, secondCopy.id);
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QCOMPARE(drive.FileCount(QStringLiteral("record")), 1);
		QVERIFY(SyncAndWait(*first.sync));
		QVERIFY(first.sync->Conflicts().empty());
		QCOMPARE(first.library->FindRecord(account.id)->label, second.library->FindRecord(secondCopy.id)->label);
	}

	// An account in Google whose file does not open is not synced, and nothing here changes because of it.
	void ReportsAnAccountItCannotReadFromGoogle() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));
		drive.DamageFiles(QStringLiteral("record"));

		const SteamForge::AuthenticatorRecord localOnly = MakeRecord(QStringLiteral("local_only"), SECOND_STEAM_ID, 0x42);
		QVERIFY(second.library->Add(localOnly).has_value());
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));

		QCOMPARE(second.library->Records().size(), std::size_t{1});
		QVERIFY(second.library->FindRecord(localOnly.id) != nullptr);
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::UNREADABLE_IN_CLOUD);
	}

	// A newer version of a synced account does not open: the copy here stays as it was, and the status says so.
	void ReportsAChangeItCannotReadFromGoogle() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);

		account.label = QStringLiteral("changed on the first computer");
		QVERIFY(first.library->Save(account).has_value());
		QVERIFY(SyncAndWait(*first.sync));
		drive.DamageFiles(QStringLiteral("record"));
		QVERIFY(SyncAndWait(*second.sync));

		const SteamForge::AuthenticatorRecord* secondCopy = second.RecordOf(FIRST_STEAM_ID);
		QVERIFY(secondCopy != nullptr);
		QVERIFY(secondCopy->label.isEmpty());
		QCOMPARE(secondCopy->secrets.sharedSecret, account.secrets.sharedSecret);
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::UNREADABLE_IN_CLOUD);

		// The next version from the first computer opens, and the sync is whole again.
		account.label = QStringLiteral("changed again on the first computer");
		QVERIFY(first.library->Save(account).has_value());
		QVERIFY(SyncAndWait(*first.sync));
		QVERIFY(SyncAndWait(*second.sync));
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QCOMPARE(second.RecordOf(FIRST_STEAM_ID)->label, QStringLiteral("changed again on the first computer"));
	}

	// An account from Google that could not be written here is not synced.
	void DoesNotCallAnUnsavedAccountSynced() {
		FakeDrive drive;
		CloudComputer first(drive);
		CloudComputer second(drive);
		const SteamForge::AuthenticatorRecord account = MakeRecord(QStringLiteral("first_player"), FIRST_STEAM_ID, 0x41);
		QVERIFY(first.library->Add(account).has_value());
		const QString recoveryKey = ConnectToGoogle(first);
		QVERIFY(AddAndWait(first, account.id));

		QFile blockingFile(second.directory.filePath(QStringLiteral("accounts")));
		QVERIFY(blockingFile.open(QIODevice::WriteOnly));
		blockingFile.close();
		QVERIFY(!ConnectToGoogle(second, recoveryKey).isEmpty());
		QVERIFY(SyncAndWait(*second.sync));

		QVERIFY(second.library->Records().empty());
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::NOT_SAVED);

		// Once the account can be written, the next sync brings it.
		QVERIFY(QFile::remove(second.directory.filePath(QStringLiteral("accounts"))));
		QVERIFY(SyncAndWait(*second.sync));
		QCOMPARE(second.sync->Status(), SteamForge::CloudSyncStatus::SYNCED);
		QVERIFY(second.RecordOf(FIRST_STEAM_ID) != nullptr);
	}
};

QTEST_GUILESS_MAIN(CloudSyncTest)

#include "cloud_sync_test.moc"
