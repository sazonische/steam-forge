#pragma once

#include "domain/account_session.h"
#include "domain/authenticator_record.h"
#include "storage/authenticator_store.h"
#include "storage/session_store.h"

namespace SteamForge {

class AccountLibrary final : public QObject {
	Q_OBJECT

public:
	AccountLibrary(AuthenticatorStore accountsStore, SessionStore sessionsStore, QObject* parent = nullptr);

	void Load();

	[[nodiscard]] const std::vector<AuthenticatorRecord>& Records() const;
	[[nodiscard]] const std::vector<StorageError>& LoadProblems() const;
	[[nodiscard]] const QString& StorageDirectory() const;
	// The pointer stays valid until the next change of the library.
	[[nodiscard]] const AuthenticatorRecord* FindRecord(const QUuid& recordId) const;
	[[nodiscard]] ImportMatch MatchExisting(const AuthenticatorRecord& candidate) const;

	[[nodiscard]] std::expected<void, StorageError> Add(const AuthenticatorRecord& record);
	// Replaces the stored record with the same ID, or adds the record when there is none.
	[[nodiscard]] std::expected<void, StorageError> Save(const AuthenticatorRecord& record);
	// Deletes the local files only; the authenticator stays active on the Steam account.
	[[nodiscard]] std::expected<void, StorageError> Remove(const QUuid& recordId);

	[[nodiscard]] std::optional<AccountSession> FindSession(const QUuid& recordId) const;
	[[nodiscard]] std::expected<void, StorageError> SaveSession(const AccountSession& session);
	[[nodiscard]] std::expected<void, StorageError> RemoveSession(const QUuid& recordId);

	// A copy of a record before it is replaced, kept apart from the accounts and sealed like them.
	[[nodiscard]] std::expected<void, StorageError> SaveBackup(const AuthenticatorRecord& record);
	// Backups that did not open; they are not accounts, so they stay out of LoadProblems().
	[[nodiscard]] const std::vector<StorageError>& BackupProblems() const;

	// Set before Load(): sealed files open only with the key.
	void SetDataKey(const std::optional<DataKey>& key);
	// Writes every loaded record, session and backup again, sealed or plain as the current key says.
	[[nodiscard]] std::expected<void, StorageError> RewriteFiles() const;

Q_SIGNALS:
	void RecordsChanged();
	void SessionChanged(const QUuid& recordId);

private:
	AuthenticatorStore authenticatorStore;
	AuthenticatorStore backupStore;
	SessionStore sessionStore;
	std::vector<AuthenticatorRecord> records;
	QHash<QUuid, AccountSession> sessions;
	std::vector<StorageError> loadProblems;
	// Held like the accounts, so a change of the key rewrites them with the key that opened them.
	std::vector<AuthenticatorRecord> backups;
	std::vector<StorageError> backupProblems;
};

} // namespace SteamForge
