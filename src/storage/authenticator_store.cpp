#include "storage/authenticator_store.h"

#include "storage/private_file.h"
#include "storage/record_serialization.h"
#include "storage/sealed_file.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView RECORD_FILE_SUFFIX{".json"};
	constexpr QLatin1StringView RECORD_FILE_PATTERN{"*.json"};
	// Larger files are not records and are not read into memory.
	constexpr qint64 MAX_RECORD_FILE_SIZE = qint64{1} << 20;

	std::expected<AuthenticatorRecord, StorageError> LoadRecordFile(const QFileInfo& fileInfo, const std::optional<DataKey>& key) {
		const std::expected<QJsonObject, StorageError> document = ReadStoredDocument(fileInfo, MAX_RECORD_FILE_SIZE, key, SealedFileKind::ACCOUNT);
		if (!document.has_value()) {
			return std::unexpected(document.error());
		}
		std::expected<AuthenticatorRecord, StorageError> record = DeserializeRecord(*document);
		if (!record.has_value()) {
			StorageError error = record.error();
			error.filePath = fileInfo.filePath();
			return std::unexpected(error);
		}
		// A copied file would otherwise produce two records with the same identity.
		if (record->id.toString(QUuid::WithoutBraces) != fileInfo.completeBaseName()) {
			return std::unexpected(StorageError{
				.kind = StorageErrorKind::INVALID_CONTENT,
				.filePath = fileInfo.filePath(),
				.detail = QStringLiteral("id"),
			});
		}
		return record;
	}
} // namespace

AuthenticatorStore::AuthenticatorStore(QString accountsDirectoryPath) :
	directoryPath(std::move(accountsDirectoryPath)) {
}

const QString& AuthenticatorStore::DirectoryPath() const {
	return directoryPath;
}

void AuthenticatorStore::SetDataKey(const std::optional<DataKey>& key) {
	dataKey = key;
}

StoreLoadResult AuthenticatorStore::LoadAll() const {
	StoreLoadResult result;
	const QDir directory(directoryPath);
	if (!directory.exists()) {
		return result;
	}

	const QFileInfoList recordFiles = directory.entryInfoList({RECORD_FILE_PATTERN}, QDir::Files, QDir::Name);
	for (const QFileInfo& recordFile : recordFiles) {
		std::expected<AuthenticatorRecord, StorageError> record = LoadRecordFile(recordFile, dataKey);
		if (record.has_value()) {
			result.records.push_back(std::move(*record));
		} else {
			qWarning("Skipped unreadable record file %s", qUtf8Printable(recordFile.fileName()));
			result.problems.push_back(record.error());
		}
	}

	std::ranges::sort(result.records, [](const AuthenticatorRecord& first, const AuthenticatorRecord& second) {
		if (first.createdAt != second.createdAt) {
			return first.createdAt < second.createdAt;
		}
		return first.id < second.id;
	});
	return result;
}

std::expected<void, StorageError> AuthenticatorStore::Save(const AuthenticatorRecord& record) const {
	if (directoryPath.isEmpty()) {
		return std::unexpected(StorageError{
			.kind = StorageErrorKind::DIRECTORY_UNAVAILABLE,
			.filePath = directoryPath,
			.detail = {},
		});
	}
	const QString filePath = RecordFilePath(record.id);
	const std::expected<QByteArray, StorageError> contents = EncodeStoredDocument(SerializeRecord(record), dataKey, SealedFileKind::ACCOUNT, filePath);
	if (!contents.has_value()) {
		return std::unexpected(contents.error());
	}
	return WritePrivateFile(filePath, *contents);
}

std::expected<void, StorageError> AuthenticatorStore::Remove(const QUuid& recordId) const {
	return RemoveFileIfExists(RecordFilePath(recordId));
}

QString AuthenticatorStore::RecordFilePath(const QUuid& recordId) const {
	return QDir(directoryPath).filePath(recordId.toString(QUuid::WithoutBraces) + RECORD_FILE_SUFFIX);
}

} // namespace SteamForge
