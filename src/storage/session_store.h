#pragma once

#include "crypto/data_sealing.h"
#include "domain/account_session.h"
#include "storage/storage_error.h"

namespace SteamForge {

struct SessionLoadResult {
	std::vector<AccountSession> sessions;
	std::vector<StorageError> problems;
};

// One file per record, separate from the authenticator secrets, so token refreshes never rewrite them.
class SessionStore final {
public:
	explicit SessionStore(QString sessionsDirectoryPath);

	// With a key, files are written sealed; plain and sealed files are read either way.
	void SetDataKey(const std::optional<DataKey>& key);
	[[nodiscard]] SessionLoadResult LoadAll() const;
	[[nodiscard]] std::expected<void, StorageError> Save(const AccountSession& session) const;
	[[nodiscard]] std::expected<void, StorageError> Remove(const QUuid& recordId) const;

private:
	[[nodiscard]] QString SessionFilePath(const QUuid& recordId) const;

	QString directoryPath;
	std::optional<DataKey> dataKey;
};

} // namespace SteamForge
