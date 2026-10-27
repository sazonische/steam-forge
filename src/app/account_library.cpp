#include "app/account_library.h"

namespace SteamForge {

namespace {
	// Next to the accounts folder, so a backup never shows up as an account.
	constexpr QLatin1StringView BACKUPS_RELATIVE_PATH{"../backups"};
} // namespace

AccountLibrary::AccountLibrary(AuthenticatorStore accountsStore, SessionStore sessionsStore, QObject* parent) :
	QObject(parent),
	authenticatorStore(std::move(accountsStore)),
	backupStore(QDir::cleanPath(QDir(authenticatorStore.DirectoryPath()).filePath(BACKUPS_RELATIVE_PATH))),
	sessionStore(std::move(sessionsStore)) {
}

void AccountLibrary::Load() {
	StoreLoadResult recordLoadResult = authenticatorStore.LoadAll();
	records = std::move(recordLoadResult.records);
	loadProblems = std::move(recordLoadResult.problems);

	SessionLoadResult sessionLoadResult = sessionStore.LoadAll();
	sessions.clear();
	for (AccountSession& session : sessionLoadResult.sessions) {
		sessions.insert(session.recordId, std::move(session));
	}
	std::ranges::move(sessionLoadResult.problems, std::back_inserter(loadProblems));
	Q_EMIT RecordsChanged();
}

const std::vector<AuthenticatorRecord>& AccountLibrary::Records() const {
	return records;
}

const std::vector<StorageError>& AccountLibrary::LoadProblems() const {
	return loadProblems;
}

const QString& AccountLibrary::StorageDirectory() const {
	return authenticatorStore.DirectoryPath();
}

const AuthenticatorRecord* AccountLibrary::FindRecord(const QUuid& recordId) const {
	const auto recordIterator = std::ranges::find(records, recordId, &AuthenticatorRecord::id);
	if (recordIterator == records.end()) {
		return nullptr;
	}
	return &*recordIterator;
}

ImportMatch AccountLibrary::MatchExisting(const AuthenticatorRecord& candidate) const {
	return MatchRecord(candidate, records);
}

std::expected<void, StorageError> AccountLibrary::Add(const AuthenticatorRecord& record) {
	if (std::expected<void, StorageError> saved = authenticatorStore.Save(record); !saved.has_value()) {
		return saved;
	}
	records.push_back(record);
	Q_EMIT RecordsChanged();
	return {};
}

std::expected<void, StorageError> AccountLibrary::Save(const AuthenticatorRecord& record) {
	const auto recordIterator = std::ranges::find(records, record.id, &AuthenticatorRecord::id);
	if (recordIterator == records.end()) {
		return Add(record);
	}
	if (std::expected<void, StorageError> saved = authenticatorStore.Save(record); !saved.has_value()) {
		return saved;
	}
	*recordIterator = record;
	Q_EMIT RecordsChanged();
	return {};
}

std::expected<void, StorageError> AccountLibrary::Remove(const QUuid& recordId) {
	if (std::expected<void, StorageError> removedSession = RemoveSession(recordId); !removedSession.has_value()) {
		return removedSession;
	}
	if (std::expected<void, StorageError> removed = authenticatorStore.Remove(recordId); !removed.has_value()) {
		return removed;
	}
	std::erase_if(records, [&recordId](const AuthenticatorRecord& record) {
		return record.id == recordId;
	});
	Q_EMIT RecordsChanged();
	return {};
}

std::expected<void, StorageError> AccountLibrary::SaveBackup(const AuthenticatorRecord& record) const {
	// A new ID, so several backups of one record stay apart.
	AuthenticatorRecord backup = record;
	backup.id = QUuid::createUuid();
	return backupStore.Save(backup);
}

void AccountLibrary::SetDataKey(const std::optional<DataKey>& key) {
	authenticatorStore.SetDataKey(key);
	backupStore.SetDataKey(key);
	sessionStore.SetDataKey(key);
}

std::expected<void, StorageError> AccountLibrary::RewriteFiles() const {
	for (const AuthenticatorRecord& record : records) {
		if (std::expected<void, StorageError> saved = authenticatorStore.Save(record); !saved.has_value()) {
			return saved;
		}
	}
	for (const AccountSession& session : sessions) {
		if (std::expected<void, StorageError> saved = sessionStore.Save(session); !saved.has_value()) {
			return saved;
		}
	}
	return {};
}

std::optional<AccountSession> AccountLibrary::FindSession(const QUuid& recordId) const {
	const auto sessionIterator = sessions.constFind(recordId);
	if (sessionIterator == sessions.constEnd()) {
		return std::nullopt;
	}
	return *sessionIterator;
}

std::expected<void, StorageError> AccountLibrary::SaveSession(const AccountSession& session) {
	if (std::expected<void, StorageError> saved = sessionStore.Save(session); !saved.has_value()) {
		return saved;
	}
	sessions.insert(session.recordId, session);
	Q_EMIT SessionChanged(session.recordId);
	return {};
}

std::expected<void, StorageError> AccountLibrary::RemoveSession(const QUuid& recordId) {
	if (std::expected<void, StorageError> removed = sessionStore.Remove(recordId); !removed.has_value()) {
		return removed;
	}
	if (sessions.remove(recordId)) {
		Q_EMIT SessionChanged(recordId);
	}
	return {};
}

} // namespace SteamForge
