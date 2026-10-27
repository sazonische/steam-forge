#include "storage/authenticator_store.h"
#include "storage/record_serialization.h"
#include "storage/sealed_file.h"

namespace {
constexpr QLatin1StringView FILLER_FIELD{"synthetic_notes"};

// A record whose document is exactly the given size, grown by a field another program left in its file.
SteamForge::AuthenticatorRecord RecordOfDocumentSize(qint64 documentSize) {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = QDateTime::currentDateTimeUtc();
	record.account.accountName = QStringLiteral("synthetic_login");
	record.source.origin = SteamForge::RecordOrigin::MA_FILE;
	record.source.importedAt = record.createdAt;
	record.secrets.sharedSecret = QByteArray(20, 's');
	record.additionalFields.insert(FILLER_FIELD, QString());
	const qint64 emptySize = SteamForge::StoredDocumentSize(SteamForge::SerializeRecord(record));
	record.additionalFields.insert(FILLER_FIELD, QString(static_cast<qsizetype>(documentSize - emptySize), QLatin1Char('n')));
	return record;
}

SteamForge::AuthenticatorRecord MakeRecord(const QString& accountName, const QDateTime& createdAt) {
	SteamForge::AuthenticatorRecord record;
	record.id = QUuid::createUuid();
	record.createdAt = createdAt;
	record.account.accountName = accountName;
	record.source.origin = SteamForge::RecordOrigin::MA_FILE;
	record.source.importedAt = createdAt;
	record.secrets.sharedSecret = QCryptographicHash::hash(accountName.toUtf8(), QCryptographicHash::Sha1);
	return record;
}

QByteArray ReadFile(const QString& filePath) {
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return file.readAll();
}

bool WriteFile(const QString& filePath, const QByteArray& contents) {
	QFile file(filePath);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}
} // namespace

class AuthenticatorStoreTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	// Whatever the store writes, sealed or plain, it reads back; a larger record is not written at all.
	void KeepsTheLargestRecordInEitherForm() {
		const QTemporaryDir temporaryDirectory;
		SteamForge::AuthenticatorStore store(temporaryDirectory.filePath(QStringLiteral("accounts")));
		const SteamForge::AuthenticatorRecord largest = RecordOfDocumentSize(SteamForge::MAX_RECORD_DOCUMENT_SIZE);
		QCOMPARE(SteamForge::StoredDocumentSize(SteamForge::SerializeRecord(largest)), SteamForge::MAX_RECORD_DOCUMENT_SIZE);
		QVERIFY(SteamForge::FitsInAccountFile(largest));
		const std::optional<SteamForge::DataKey> key = SteamForge::DataKey::Generate();
		QVERIFY(key.has_value());
		for (const std::optional<SteamForge::DataKey>& storeKey : {std::optional<SteamForge::DataKey>(), key}) {
			store.SetDataKey(storeKey);
			QVERIFY(store.Save(largest).has_value());
			const SteamForge::StoreLoadResult loadResult = store.LoadAll();
			QVERIFY(loadResult.problems.empty());
			QCOMPARE(loadResult.records.size(), std::size_t{1});
			QCOMPARE(loadResult.records.front().additionalFields, largest.additionalFields);
		}

		const SteamForge::AuthenticatorRecord tooLarge = RecordOfDocumentSize(SteamForge::MAX_RECORD_DOCUMENT_SIZE + 1);
		QVERIFY(!SteamForge::FitsInAccountFile(tooLarge));
		const std::expected<void, SteamForge::StorageError> saved = store.Save(tooLarge);
		QVERIFY(!saved.has_value());
		QCOMPARE(saved.error().kind, SteamForge::StorageErrorKind::FILE_TOO_LARGE);
		QCOMPARE(store.LoadAll().records.size(), std::size_t{1});
	}

	void MissingDirectoryMeansNoRecords() {
		const QTemporaryDir temporaryDirectory;
		const SteamForge::AuthenticatorStore store(temporaryDirectory.filePath(QStringLiteral("accounts")));
		const SteamForge::StoreLoadResult loadResult = store.LoadAll();
		QVERIFY(loadResult.records.empty());
		QVERIFY(loadResult.problems.empty());
	}

	void SavedRecordsLoadInCreationOrder() {
		const QTemporaryDir temporaryDirectory;
		const SteamForge::AuthenticatorStore store(temporaryDirectory.filePath(QStringLiteral("accounts")));
		const QDateTime now = QDateTime::currentDateTimeUtc();
		const SteamForge::AuthenticatorRecord newerRecord = MakeRecord(QStringLiteral("second"), now);
		const SteamForge::AuthenticatorRecord olderRecord = MakeRecord(QStringLiteral("first"), now.addSecs(-60));
		QVERIFY(store.Save(newerRecord).has_value());
		QVERIFY(store.Save(olderRecord).has_value());

		const SteamForge::StoreLoadResult loadResult = store.LoadAll();
		QCOMPARE(loadResult.records.size(), std::size_t{2});
		QCOMPARE(loadResult.records.at(0).id, olderRecord.id);
		QCOMPARE(loadResult.records.at(1).id, newerRecord.id);
		QCOMPARE(loadResult.records.at(0).secrets.sharedSecret, olderRecord.secrets.sharedSecret);
	}

	void SavingAgainReplacesTheFileWithoutLeftovers() {
		const QTemporaryDir temporaryDirectory;
		const QString accountsPath = temporaryDirectory.filePath(QStringLiteral("accounts"));
		const SteamForge::AuthenticatorStore store(accountsPath);
		SteamForge::AuthenticatorRecord record = MakeRecord(QStringLiteral("user"), QDateTime::currentDateTimeUtc());
		QVERIFY(store.Save(record).has_value());
		record.label = QStringLiteral("Renamed");
		QVERIFY(store.Save(record).has_value());

		QCOMPARE(QDir(accountsPath).entryList(QDir::Files | QDir::Hidden).size(), 1);
		const SteamForge::StoreLoadResult loadResult = store.LoadAll();
		QCOMPARE(loadResult.records.size(), std::size_t{1});
		QCOMPARE(loadResult.records.front().label, QStringLiteral("Renamed"));
	}

	void DamagedFilesDoNotHideOtherRecordsAndStayUnchanged() {
		const QTemporaryDir temporaryDirectory;
		const QString accountsPath = temporaryDirectory.filePath(QStringLiteral("accounts"));
		const SteamForge::AuthenticatorStore store(accountsPath);
		const SteamForge::AuthenticatorRecord record = MakeRecord(QStringLiteral("healthy"), QDateTime::currentDateTimeUtc());
		QVERIFY(store.Save(record).has_value());

		const QDir accountsDirectory(accountsPath);
		const QString truncatedPath = accountsDirectory.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json"));
		const QByteArray truncatedContents = QByteArray(R"({"format":"steamforge-authenticator","formatVersion":1})").first(40);
		QVERIFY(WriteFile(truncatedPath, truncatedContents));
		const QString newerPath = accountsDirectory.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json"));
		const QByteArray newerContents = R"({"format":"steamforge-authenticator","formatVersion":99})";
		QVERIFY(WriteFile(newerPath, newerContents));

		const SteamForge::StoreLoadResult loadResult = store.LoadAll();
		QCOMPARE(loadResult.records.size(), std::size_t{1});
		QCOMPARE(loadResult.records.front().id, record.id);
		QCOMPARE(loadResult.problems.size(), std::size_t{2});
		QVERIFY(std::ranges::any_of(loadResult.problems, [](const SteamForge::StorageError& problem) {
			return problem.kind == SteamForge::StorageErrorKind::NEWER_FORMAT_VERSION;
		}));
		QCOMPARE(ReadFile(truncatedPath), truncatedContents);
		QCOMPARE(ReadFile(newerPath), newerContents);
	}

	void CopiedFileUnderAnotherNameIsRejected() {
		const QTemporaryDir temporaryDirectory;
		const QString accountsPath = temporaryDirectory.filePath(QStringLiteral("accounts"));
		const SteamForge::AuthenticatorStore store(accountsPath);
		const SteamForge::AuthenticatorRecord record = MakeRecord(QStringLiteral("original"), QDateTime::currentDateTimeUtc());
		QVERIFY(store.Save(record).has_value());
		const QDir accountsDirectory(accountsPath);
		const QString originalPath = accountsDirectory.filePath(record.id.toString(QUuid::WithoutBraces) + QStringLiteral(".json"));
		QVERIFY(QFile::copy(originalPath, accountsDirectory.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json"))));

		const SteamForge::StoreLoadResult loadResult = store.LoadAll();
		QCOMPARE(loadResult.records.size(), std::size_t{1});
		QCOMPARE(loadResult.problems.size(), std::size_t{1});
		QCOMPARE(loadResult.problems.front().kind, SteamForge::StorageErrorKind::INVALID_CONTENT);
	}

	void RemoveDeletesOnlyTheChosenRecord() {
		const QTemporaryDir temporaryDirectory;
		const SteamForge::AuthenticatorStore store(temporaryDirectory.filePath(QStringLiteral("accounts")));
		const SteamForge::AuthenticatorRecord keptRecord = MakeRecord(QStringLiteral("kept"), QDateTime::currentDateTimeUtc());
		const SteamForge::AuthenticatorRecord removedRecord = MakeRecord(QStringLiteral("removed"), QDateTime::currentDateTimeUtc());
		QVERIFY(store.Save(keptRecord).has_value());
		QVERIFY(store.Save(removedRecord).has_value());

		QVERIFY(store.Remove(removedRecord.id).has_value());
		const SteamForge::StoreLoadResult loadResult = store.LoadAll();
		QCOMPARE(loadResult.records.size(), std::size_t{1});
		QCOMPARE(loadResult.records.front().id, keptRecord.id);
		QVERIFY(store.Remove(removedRecord.id).has_value());
	}

	void UnwritableLocationReportsAnError() {
		const QTemporaryDir temporaryDirectory;
		const QString blockingFilePath = temporaryDirectory.filePath(QStringLiteral("not-a-directory"));
		QVERIFY(WriteFile(blockingFilePath, "x"));
		const SteamForge::AuthenticatorStore store(QDir(blockingFilePath).filePath(QStringLiteral("accounts")));
		const std::expected<void, SteamForge::StorageError> saved = store.Save(MakeRecord(QStringLiteral("user"), QDateTime::currentDateTimeUtc()));
		QVERIFY(!saved.has_value());
		QCOMPARE(saved.error().kind, SteamForge::StorageErrorKind::DIRECTORY_UNAVAILABLE);
	}

	void FilesAreReadableOnlyByTheOwner() {
#ifdef Q_OS_WIN
		QSKIP("Windows protects the data folder with the profile ACL.");
#else
		const QTemporaryDir temporaryDirectory;
		const QString accountsPath = temporaryDirectory.filePath(QStringLiteral("accounts"));
		const SteamForge::AuthenticatorStore store(accountsPath);
		const SteamForge::AuthenticatorRecord record = MakeRecord(QStringLiteral("user"), QDateTime::currentDateTimeUtc());
		QVERIFY(store.Save(record).has_value());

		const QFileDevice::Permissions groupOrOtherAccess = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
		const QString recordPath = QDir(accountsPath).filePath(record.id.toString(QUuid::WithoutBraces) + QStringLiteral(".json"));
		QCOMPARE(QFileInfo(recordPath).permissions() & groupOrOtherAccess, QFileDevice::Permissions());
		QCOMPARE(QFileInfo(accountsPath).permissions() & groupOrOtherAccess, QFileDevice::Permissions());
#endif
	}
};

QTEST_GUILESS_MAIN(AuthenticatorStoreTest)

#include "authenticator_store_test.moc"
