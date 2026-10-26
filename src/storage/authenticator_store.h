#pragma once

#include "crypto/data_sealing.h"
#include "domain/authenticator_record.h"
#include "storage/storage_error.h"

namespace SteamForge {

struct StoreLoadResult {
	std::vector<AuthenticatorRecord> records;
	// Unreadable files stay untouched and do not hide the other records.
	std::vector<StorageError> problems;
};

class AuthenticatorStore final {
public:
	explicit AuthenticatorStore(QString accountsDirectoryPath);

	[[nodiscard]] const QString& DirectoryPath() const;
	// With a key, files are written sealed; plain and sealed files are read either way.
	void SetDataKey(const std::optional<DataKey>& key);
	[[nodiscard]] StoreLoadResult LoadAll() const;
	[[nodiscard]] std::expected<void, StorageError> Save(const AuthenticatorRecord& record) const;
	[[nodiscard]] std::expected<void, StorageError> Remove(const QUuid& recordId) const;

private:
	[[nodiscard]] QString RecordFilePath(const QUuid& recordId) const;

	QString directoryPath;
	std::optional<DataKey> dataKey;
};

} // namespace SteamForge
