#include "app/account_library.h"
#include "app/storage_encryption.h"
#include "storage/authenticator_import.h"
#include "support/fake_key_store.h"

namespace {
constexpr quint64 RECORD_STEAM_ID = 76561198000000001;
constexpr QLatin1StringView ACCOUNTS_DIRECTORY{"accounts"};
constexpr QLatin1StringView SESSIONS_DIRECTORY{"sessions"};
constexpr QLatin1StringView BACKUPS_DIRECTORY{"backups"};
constexpr QLatin1StringView STATE_FILE{"encryption.json"};
constexpr QByteArrayView SEALED_MARKER{"steamforge-sealed"};
constexpr QByteArrayView REFRESH_TOKEN{"synthetic.refresh.token"};
constexpr char FIRST_SECRET_SEED = 's';
constexpr char SECOND_SECRET_SEED = 't';
constexpr qsizetype SECRET_LENGTH = 20;
constexpr qsizetype LARGE_FIELD_LENGTH = qsizetype{800} << 10;

SteamForge::AuthenticatorRecord MakeRecord(const QString& accountName, char secretSeed = FIRST_SECRET_SEED) {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.source.origin = SteamForge::RecordOrigin::MA_FILE;
	record.source.importedAt = record.createdAt;
	record.account.steamId = RECORD_STEAM_ID;
	record.account.accountName = accountName;
	record.secrets.sharedSecret = QByteArray(SECRET_LENGTH, secretSeed);
	return record;
}

SteamForge::AccountSession MakeSession(const SteamForge::AuthenticatorRecord& record) {
	return {
		.recordId = record.id,
		.steamId = RECORD_STEAM_ID,
		.accountName = record.account.accountName,
		.refreshToken = QString::fromLatin1(REFRESH_TOKEN),
		.accessToken = QStringLiteral("synthetic.access.token"),
		.signedInAt = QDateTime::currentDateTimeUtc(),
	};
}

std::unique_ptr<SteamForge::AccountLibrary> LibraryIn(const QTemporaryDir& directory) {
	return std::make_unique<SteamForge::AccountLibrary>(SteamForge::AuthenticatorStore(directory.filePath(ACCOUNTS_DIRECTORY)), SteamForge::SessionStore(directory.filePath(SESSIONS_DIRECTORY)));
}

QByteArray FileBytes(const QString& filePath) {
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

QString RecordFile(const QTemporaryDir& directory, const QUuid& recordId) {
	return QDir(directory.filePath(ACCOUNTS_DIRECTORY)).filePath(recordId.toString(QUuid::WithoutBraces) + QStringLiteral(".json"));
}

QString SessionFile(const QTemporaryDir& directory, const QUuid& recordId) {
	return QDir(directory.filePath(SESSIONS_DIRECTORY)).filePath(recordId.toString(QUuid::WithoutBraces) + QStringLiteral(".json"));
}

QStringList BackupFiles(const QTemporaryDir& directory) {
	const QDir backups(directory.filePath(BACKUPS_DIRECTORY));
	QStringList filePaths;
	for (const QString& fileName : backups.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name)) {
		filePaths.append(backups.filePath(fileName));
	}
	return filePaths;
}

bool AreAllBackupsSealed(const QTemporaryDir& directory) {
	const QStringList filePaths = BackupFiles(directory);
	return !filePaths.isEmpty() && std::ranges::all_of(filePaths, [](const QString& filePath) {
		return FileBytes(filePath).contains(SEALED_MARKER.toByteArray());
	});
}

bool AreAllBackupsPlain(const QTemporaryDir& directory) {
	const QStringList filePaths = BackupFiles(directory);
	return !filePaths.isEmpty() && std::ranges::all_of(filePaths, [](const QString& filePath) {
		return FileBytes(filePath).contains("synthetic_login");
	});
}

// A library with one record and its sign-in, written as plain files.
SteamForge::AuthenticatorRecord AddPlainAccount(const QTemporaryDir& directory, char secretSeed = FIRST_SECRET_SEED) {
	const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
	library->Load();
	SteamForge::AuthenticatorRecord record = MakeRecord(QStringLiteral("synthetic_login"), secretSeed);
	if (!library->Add(record).has_value() || !library->SaveSession(MakeSession(record)).has_value()) {
		return {};
	}
	return record;
}
} // namespace

class StorageEncryptionTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void SealsEveryFileAndOpensThemAfterRestart() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		{
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::OFF);
			QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
			QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::ON);
		}
		const QByteArray recordBytes = FileBytes(RecordFile(directory, record.id));
		const QByteArray sessionBytes = FileBytes(SessionFile(directory, record.id));
		QVERIFY(recordBytes.contains(SEALED_MARKER.toByteArray()));
		QVERIFY(!recordBytes.contains("synthetic_login"));
		QVERIFY(!recordBytes.contains(record.secrets.sharedSecret.toBase64()));
		QVERIFY(sessionBytes.contains(SEALED_MARKER.toByteArray()));
		QVERIFY(!sessionBytes.contains(REFRESH_TOKEN.toByteArray()));
		QCOMPARE(keyStore.KeyCount(), qsizetype{1});

		const std::unique_ptr<SteamForge::AccountLibrary> restarted = LibraryIn(directory);
		SteamForge::StorageEncryption restartedEncryption(keyStore, *restarted, directory.filePath(STATE_FILE));
		restartedEncryption.PrepareLibrary();
		restarted->Load();
		QCOMPARE(restartedEncryption.Status(), SteamForge::EncryptionStatus::ON);
		QVERIFY(restarted->LoadProblems().empty());
		QCOMPARE(restarted->FindRecord(record.id)->secrets.sharedSecret, record.secrets.sharedSecret);
		QCOMPARE(restarted->FindSession(record.id)->refreshToken, QString::fromLatin1(REFRESH_TOKEN));
	}

	void SealsRecordsAddedWhileOn() {
		const QTemporaryDir directory;
		FakeKeyStore keyStore;
		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());

		const SteamForge::AuthenticatorRecord record = MakeRecord(QStringLiteral("added_later"), SECOND_SECRET_SEED);
		QVERIFY(library->Add(record).has_value());
		QVERIFY(FileBytes(RecordFile(directory, record.id)).contains(SEALED_MARKER.toByteArray()));
	}

	void TurnsOffToPlainFilesAndForgetsTheKey() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		const QSignalSpy statusSpy(&encryption, &SteamForge::StorageEncryption::StatusChanged);

		QVERIFY(encryption.Disable().has_value());
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::OFF);
		QCOMPARE(statusSpy.count(), 1);
		QVERIFY(FileBytes(RecordFile(directory, record.id)).contains("synthetic_login"));
		QVERIFY(FileBytes(SessionFile(directory, record.id)).contains(REFRESH_TOKEN.toByteArray()));
		QVERIFY(!QFileInfo::exists(directory.filePath(STATE_FILE)));
		QCOMPARE(keyStore.KeyCount(), qsizetype{0});
	}

	// Backups of replaced Steam Guards change format with the accounts: a backup left sealed would lose its key.
	void KeepsBackupsOpenThroughEveryChange() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		{
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QVERIFY(library->SaveBackup(record).has_value());
			QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
			QVERIFY(library->SaveBackup(MakeRecord(QStringLiteral("synthetic_login"), SECOND_SECRET_SEED)).has_value());
			QCOMPARE(BackupFiles(directory).size(), qsizetype{2});
			QVERIFY(AreAllBackupsSealed(directory));

			QVERIFY(encryption.Disable().has_value());
			QCOMPARE(keyStore.KeyCount(), qsizetype{0});
			QVERIFY(AreAllBackupsPlain(directory));

			QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
			QVERIFY(AreAllBackupsSealed(directory));
		}

		// After a restart the backups open with the new key and turn plain again.
		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QVERIFY(library->BackupProblems().empty());
		QVERIFY(encryption.Disable().has_value());
		QCOMPARE(BackupFiles(directory).size(), qsizetype{2});
		QVERIFY(AreAllBackupsPlain(directory));
	}

	void KeepsTheKeyWhileABackupDoesNotOpen() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		{
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
			QVERIFY(library->SaveBackup(record).has_value());
		}
		// The account file under the name of the backup: sealed with this key, it does not open there.
		const QStringList backupFiles = BackupFiles(directory);
		QCOMPARE(backupFiles.size(), qsizetype{1});
		QFile backupFile(backupFiles.front());
		QVERIFY(backupFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
		backupFile.write(FileBytes(RecordFile(directory, record.id)));
		backupFile.close();
		const QByteArray backupBytes = FileBytes(backupFiles.front());

		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QVERIFY(library->LoadProblems().empty());
		QCOMPARE(library->BackupProblems().size(), std::size_t{1});
		const std::expected<void, SteamForge::EncryptionFailure> disabled = encryption.Disable();
		QVERIFY(!disabled.has_value());
		QVERIFY(disabled.error().unreadableFile.has_value());
		QCOMPARE(disabled.error().unreadableFile->filePath, backupFiles.front());
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::ON);
		QCOMPARE(keyStore.KeyCount(), qsizetype{1});
		QCOMPARE(FileBytes(backupFiles.front()), backupBytes);
		QVERIFY(FileBytes(RecordFile(directory, record.id)).contains(SEALED_MARKER.toByteArray()));

		// A change cut short before is not finished at the next start either.
		QJsonObject state = QJsonDocument::fromJson(FileBytes(directory.filePath(STATE_FILE))).object();
		state.insert(QStringLiteral("state"), QStringLiteral("disabling"));
		QFile stateFile(directory.filePath(STATE_FILE));
		QVERIFY(stateFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
		stateFile.write(QJsonDocument(state).toJson());
		stateFile.close();
		const std::unique_ptr<SteamForge::AccountLibrary> restarted = LibraryIn(directory);
		SteamForge::StorageEncryption restartedEncryption(keyStore, *restarted, directory.filePath(STATE_FILE));
		restartedEncryption.PrepareLibrary();
		restarted->Load();
		const std::expected<void, SteamForge::EncryptionFailure> finished = restartedEncryption.FinishInterruptedChange();
		QVERIFY(!finished.has_value());
		QVERIFY(finished.error().unreadableFile.has_value());
		QCOMPARE(restartedEncryption.Status(), SteamForge::EncryptionStatus::ON);
		QCOMPARE(keyStore.KeyCount(), qsizetype{1});
		QVERIFY(QFileInfo::exists(directory.filePath(STATE_FILE)));
		QVERIFY(FileBytes(RecordFile(directory, record.id)).contains(SEALED_MARKER.toByteArray()));
	}

	// An imported file may carry large fields of another program; sealed, the record grows by a third.
	void OpensALargeRecordAfterSealing() {
		const QTemporaryDir directory;
		FakeKeyStore keyStore;
		const QJsonObject maFile{
			{QStringLiteral("shared_secret"), QString::fromLatin1(QByteArray(SECRET_LENGTH, FIRST_SECRET_SEED).toBase64())},
			{QStringLiteral("account_name"), QStringLiteral("synthetic_login")},
			{QStringLiteral("synthetic_notes"), QString(LARGE_FIELD_LENGTH, QLatin1Char('n'))},
		};
		const std::expected<SteamForge::ImportResult, SteamForge::ImportError> imported = SteamForge::ParseMaFile(QJsonDocument(maFile).toJson(), QStringLiteral("synthetic.maFile"));
		QVERIFY(imported.has_value());
		QCOMPARE(imported->records.size(), std::size_t{1});
		const SteamForge::AuthenticatorRecord record = imported->records.front();
		{
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QVERIFY(library->Add(record).has_value());
			QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		}
		QVERIFY(FileBytes(RecordFile(directory, record.id)).contains(SEALED_MARKER.toByteArray()));

		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QVERIFY(library->LoadProblems().empty());
		const SteamForge::AuthenticatorRecord* loaded = library->FindRecord(record.id);
		QVERIFY(loaded != nullptr);
		QCOMPARE(loaded->additionalFields, record.additionalFields);
	}

	void ChangesNothingWhenTheSystemKeepsNoKey() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		const QByteArray plainBytes = FileBytes(RecordFile(directory, record.id));
		FakeKeyStore keyStore;
		keyStore.saveFailure = SteamForge::KeyStoreFailure::DISMISSED;
		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();

		const std::expected<void, SteamForge::EncryptionFailure> enabled = encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT);
		QVERIFY(!enabled.has_value());
		QCOMPARE(enabled.error().keyStoreFailure, std::optional(SteamForge::KeyStoreFailure::DISMISSED));
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::OFF);
		QCOMPARE(FileBytes(RecordFile(directory, record.id)), plainBytes);
		QVERIFY(!QFileInfo::exists(directory.filePath(STATE_FILE)));
	}

	void LeavesSealedFilesAloneWithoutTheKey() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		{
			FakeKeyStore keyStore;
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		}
		const QByteArray sealedBytes = FileBytes(RecordFile(directory, record.id));

		// Another user account or computer: the system gives no key.
		FakeKeyStore otherKeyStore;
		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(otherKeyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::KEY_UNAVAILABLE);
		QVERIFY(library->Records().empty());
		// The account file and its sign-in file both stay closed.
		QCOMPARE(library->LoadProblems().size(), std::size_t{2});
		QVERIFY(std::ranges::all_of(library->LoadProblems(), [](const SteamForge::StorageError& problem) {
			return problem.kind == SteamForge::StorageErrorKind::KEY_UNAVAILABLE;
		}));
		QVERIFY(!encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		QVERIFY(!encryption.Disable().has_value());
		QCOMPARE(FileBytes(RecordFile(directory, record.id)), sealedBytes);
	}

	void KeepsFilesSealedWhenTheStateFileIsGone() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		{
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		}
		const QByteArray stateBytes = FileBytes(directory.filePath(STATE_FILE));
		const QByteArray sealedBytes = FileBytes(RecordFile(directory, record.id));
		QVERIFY(QFile::remove(directory.filePath(STATE_FILE)));

		{
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::OFF);
			QVERIFY(library->Records().empty());
			const std::expected<void, SteamForge::EncryptionFailure> enabled = encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT);
			QVERIFY(!enabled.has_value());
			QVERIFY(enabled.error().unreadableFile.has_value());
			QCOMPARE(keyStore.KeyCount(), qsizetype{1});
			QVERIFY(!QFileInfo::exists(directory.filePath(STATE_FILE)));
			QCOMPARE(FileBytes(RecordFile(directory, record.id)), sealedBytes);
		}

		// Put back, the state file opens everything again.
		QFile stateFile(directory.filePath(STATE_FILE));
		QVERIFY(stateFile.open(QIODevice::WriteOnly));
		stateFile.write(stateBytes);
		stateFile.close();
		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::ON);
		QVERIFY(library->FindRecord(record.id) != nullptr);
	}

	void FinishesAnInterruptedChange() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		const std::optional<SteamForge::DataKey> key = SteamForge::DataKey::Generate();
		QVERIFY(key.has_value());
		const QString vaultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		QVERIFY(keyStore.Save(vaultId, *key, SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		// The program stopped after saving the key and the state, before any file was sealed.
		QFile stateFile(directory.filePath(STATE_FILE));
		QVERIFY(stateFile.open(QIODevice::WriteOnly));
		stateFile.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("steamforge-encryption")}, {QStringLiteral("formatVersion"), 1}, {QStringLiteral("state"), QStringLiteral("enabling")}, {QStringLiteral("vaultId"), vaultId}}).toJson());
		stateFile.close();

		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QVERIFY(library->FindRecord(record.id) != nullptr);
		QVERIFY(encryption.FinishInterruptedChange().has_value());
		QVERIFY(FileBytes(RecordFile(directory, record.id)).contains(SEALED_MARKER.toByteArray()));
		QVERIFY(FileBytes(directory.filePath(STATE_FILE)).contains("\"enabled\""));
	}

	void SwappedFilesDoNotOpen() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord first = AddPlainAccount(directory);
		const SteamForge::AuthenticatorRecord second = AddPlainAccount(directory, SECOND_SECRET_SEED);
		FakeKeyStore keyStore;
		{
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QVERIFY(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		}
		QFile secondFile(RecordFile(directory, second.id));
		QVERIFY(secondFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
		secondFile.write(FileBytes(RecordFile(directory, first.id)));
		secondFile.close();

		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QVERIFY(library->FindRecord(first.id) != nullptr);
		QVERIFY(library->FindRecord(second.id) == nullptr);
		QCOMPARE(library->LoadProblems().front().kind, SteamForge::StorageErrorKind::UNSEALING_FAILED);
	}

	void WaitsForTheUserBeforeLoading_data() {
		QTest::addColumn<SteamForge::KeyProtection>("protection");
		QTest::newRow("Windows Hello") << SteamForge::KeyProtection::WINDOWS_HELLO;
		QTest::newRow("keyring password") << SteamForge::KeyProtection::KEYRING_PASSWORD;
	}

	void WaitsForTheUserBeforeLoading() {
		QFETCH(SteamForge::KeyProtection, protection);
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		keyStore.preferredProtection = protection;
		{
			const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
			SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
			encryption.PrepareLibrary();
			library->Load();
			QCOMPARE(encryption.PreferredProtection(), protection);
			QVERIFY(encryption.Enable(protection).has_value());
			QCOMPARE(keyStore.lastProtection, std::optional(protection));
		}

		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		const int loadsBefore = keyStore.loadCount;
		encryption.PrepareLibrary();
		// Nothing is asked and nothing is read until the user unlocks.
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::LOCKED);
		QCOMPARE(keyStore.loadCount, loadsBefore);
		QVERIFY(library->Records().empty());

		keyStore.loadFailure = SteamForge::KeyStoreFailure::DISMISSED;
		QCOMPARE(encryption.Unlock().error().keyStoreFailure, std::optional(SteamForge::KeyStoreFailure::DISMISSED));
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::LOCKED);
		QVERIFY(library->Records().empty());

		keyStore.loadFailure.reset();
		const QSignalSpy recordsSpy(library.get(), &SteamForge::AccountLibrary::RecordsChanged);
		QVERIFY(encryption.Unlock().has_value());
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::ON);
		QCOMPARE(recordsSpy.count(), 1);
		QVERIFY(library->FindRecord(record.id) != nullptr);
		QCOMPARE(encryption.Protection(), protection);
	}

	void ConfirmsTurningOffWithTheUser_data() {
		WaitsForTheUserBeforeLoading_data();
	}

	void ConfirmsTurningOffWithTheUser() {
		QFETCH(SteamForge::KeyProtection, protection);
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		keyStore.preferredProtection = protection;
		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		library->Load();
		QVERIFY(encryption.Enable(protection).has_value());

		keyStore.loadFailure = SteamForge::KeyStoreFailure::DISMISSED;
		QVERIFY(!encryption.Disable().has_value());
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::ON);
		QVERIFY(FileBytes(RecordFile(directory, record.id)).contains(SEALED_MARKER.toByteArray()));

		keyStore.loadFailure.reset();
		QVERIFY(encryption.Disable().has_value());
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::OFF);
	}

	void ReadsStateWrittenBeforeWindowsHello() {
		const QTemporaryDir directory;
		const SteamForge::AuthenticatorRecord record = AddPlainAccount(directory);
		FakeKeyStore keyStore;
		const std::optional<SteamForge::DataKey> key = SteamForge::DataKey::Generate();
		QVERIFY(key.has_value());
		const QString vaultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
		QVERIFY(keyStore.Save(vaultId, *key, SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		QFile stateFile(directory.filePath(STATE_FILE));
		QVERIFY(stateFile.open(QIODevice::WriteOnly));
		stateFile.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), QStringLiteral("steamforge-encryption")}, {QStringLiteral("formatVersion"), 1}, {QStringLiteral("state"), QStringLiteral("enabled")}, {QStringLiteral("vaultId"), vaultId}}).toJson());
		stateFile.close();

		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::ON);
		QCOMPARE(encryption.Protection(), SteamForge::KeyProtection::USER_ACCOUNT);
		library->Load();
		QVERIFY(library->FindRecord(record.id) != nullptr);
	}

	void ReportsASystemWithoutKeyStore() {
		const QTemporaryDir directory;
		FakeKeyStore keyStore;
		keyStore.isAvailable = false;
		const std::unique_ptr<SteamForge::AccountLibrary> library = LibraryIn(directory);
		SteamForge::StorageEncryption encryption(keyStore, *library, directory.filePath(STATE_FILE));
		encryption.PrepareLibrary();
		QCOMPARE(encryption.Status(), SteamForge::EncryptionStatus::UNSUPPORTED);
		QCOMPARE(encryption.Enable(SteamForge::KeyProtection::USER_ACCOUNT).error().keyStoreFailure, std::optional(SteamForge::KeyStoreFailure::UNAVAILABLE));
	}
};

QTEST_MAIN(StorageEncryptionTest)

#include "storage_encryption_test.moc"
