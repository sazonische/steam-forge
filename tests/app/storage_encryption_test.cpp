#include "app/account_library.h"
#include "app/storage_encryption.h"
#include "support/fake_key_store.h"

namespace {
constexpr quint64 RECORD_STEAM_ID = 76561198000000001;
constexpr QLatin1StringView ACCOUNTS_DIRECTORY{"accounts"};
constexpr QLatin1StringView SESSIONS_DIRECTORY{"sessions"};
constexpr QLatin1StringView STATE_FILE{"encryption.json"};
constexpr QByteArrayView SEALED_MARKER{"steamforge-sealed"};
constexpr QByteArrayView REFRESH_TOKEN{"synthetic.refresh.token"};
constexpr char FIRST_SECRET_SEED = 's';
constexpr char SECOND_SECRET_SEED = 't';
constexpr qsizetype SECRET_LENGTH = 20;

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
