#pragma once

#include "cloud/google_drive.h"
#include "domain/authenticator_record.h"
#include "storage/storage_error.h"

#include <deque>
#include <map>

namespace SteamForge {

class AccountLibrary;
class GoogleConnection;
struct CloudLink;

enum class CloudSyncStatus {
	// Google is not connected or not unlocked.
	INACTIVE,
	SYNCING,
	SYNCED,
	// A conflict waits for the user; the other accounts keep syncing.
	NEEDS_DECISION,
	UNREACHABLE,
	// A change could not be written on this computer; the next sync tries again.
	NOT_SAVED,
	// A record in Google does not open: damaged, or saved by a newer SteamForge. The accounts here stay as they are.
	UNREADABLE_IN_CLOUD
};

enum class CloudConflictKind {
	// The cloud has another Steam Guard of an account this computer keeps: Steam accepts only one of them.
	OTHER_KEY_OF_ACCOUNT,
	// The account changed here and on another computer since the last sync.
	CHANGED_ON_BOTH
};

struct CloudConflict {
	CloudConflictKind kind = CloudConflictKind::OTHER_KEY_OF_ACCOUNT;
	QUuid localRecordId;
	AuthenticatorRecord cloudRecord;
	QString cloudRecordId;
	int cloudRevision = 1;
	QString cloudFileId;
};

enum class ConflictChoice {
	// Only for another key of an account: the cloud one comes as a separate account.
	KEEP_BOTH,
	// The local record takes the cloud version; the local one is kept as a backup first.
	USE_CLOUD,
	USE_LOCAL
};

// Keeps the accounts that are in Google the same on every computer. An account is either in Google (linked to a
// cloud record) or only on this computer; nothing is ever merged or overwritten because of the SteamID alone, and
// no Steam Guard key is deleted from a computer because it left the cloud.
class CloudSync final : public QObject {
	Q_OBJECT

public:
	CloudSync(GoogleConnection& connection, AccountLibrary& library, QObject* parent = nullptr);

	// At the start and every ten minutes; local changes sync a few seconds after they happen.
	void Start();
	void Stop();
	void Synchronize();
	// The actions below run after a pass that is running: its plan was made without them.
	void AddToCloud(const QUuid& recordId);
	// The cloud record goes; the account stays here and, unlinked, on the other computers.
	void RemoveFromCloud(const QUuid& recordId);
	void ResolveConflict(const QString& cloudRecordId, ConflictChoice choice);
	// After the account was removed from this computer only: the cloud record stays for the other computers and
	// does not come back here.
	void LeaveInCloud(const QUuid& recordId, const QString& title);
	// A record left in the cloud comes back to this computer on the next pass.
	void ReturnFromCloud(const QString& cloudRecordId);

	[[nodiscard]] CloudSyncStatus Status() const;
	[[nodiscard]] const QDateTime& LastSyncedAt() const;
	[[nodiscard]] const std::vector<CloudConflict>& Conflicts() const;
	[[nodiscard]] bool IsInCloud(const QUuid& recordId) const;
	[[nodiscard]] bool HasConflict(const QUuid& recordId) const;
	// An action asked for the account has not finished yet.
	[[nodiscard]] bool IsChanging(const QUuid& recordId) const;

Q_SIGNALS:
	void StatusChanged();
	// An account of the cloud came to this computer.
	void RecordAdded(const QUuid& recordId);
	// Another computer removed the account from Google; it stays here, only on this computer.
	void RecordUnlinked(const QUuid& recordId);
	// The Steam Guard of the account here became the one from Google; the previous one is in a backup.
	void KeyReplaced(const QUuid& recordId);
	// The end of AddToCloud; false when the account stays only on this computer.
	void AddingFinished(const QUuid& recordId, bool isAdded);

private:
	struct CloudRevision {
		QString recordId;
		int revision = 1;
		DriveFile file;
	};

	using Step = std::function<void()>;
	// Newest revision first.
	using RevisionsByRecord = std::map<QString, std::vector<CloudRevision>>;

	// Until the step has run, passes leave both records alone.
	void Request(const QUuid& localRecordId, const QString& cloudRecordId, Step step);
	void RunRequestedSteps();
	void ApplyDecision(const CloudConflict& conflict, ConflictChoice choice);
	// A run is the steps asked for and the passes after them; what failed in it shows until the next run.
	void BeginRun();
	void StartPass();
	// Logs a failed write and keeps the run from counting as synced.
	bool CheckSaved(const std::expected<void, StorageError>& saved);
	void PlanPass(const std::vector<DriveFile>& files);
	[[nodiscard]] RevisionsByRecord RecordRevisions(const std::vector<DriveFile>& files) const;
	// Forgets the records that left the cloud.
	[[nodiscard]] QSet<QString> CurrentCloudOnlyRecordIds(const RevisionsByRecord& revisionsByRecord);
	void PlanLink(const CloudLink& link, const RevisionsByRecord& revisionsByRecord);
	void RunNextStep();
	void FinishPass();
	// done tells whether the record here and its link took the version.
	void ApplyCloudVersion(CloudLink link, const CloudRevision& revision, std::function<void(bool)> done);
	void PushLocalVersion(CloudLink link, const AuthenticatorRecord& record, int baseRevision, std::function<void(bool)> done);
	void AdoptCloudRecord(const CloudRevision& revision);
	void LinkSameGuard(AuthenticatorRecord local, const AuthenticatorRecord& cloudRecord, const CloudRevision& revision);
	void CollectConflict(CloudConflictKind kind, const QUuid& localRecordId, const CloudRevision& revision);
	void UploadNewRecord(const AuthenticatorRecord& record, std::function<void(bool)> done);
	// False as soon as a file could not be deleted; a file that is already gone counts as deleted.
	void DeleteFiles(std::vector<QString> fileIds, std::function<void(bool)> done);
	// After a decision is in place: the older revisions and the version this computer had would bring the question
	// back. A newer revision is a change of another computer and stays.
	void KeepOnlyLinkedRevision(const QUuid& localRecordId, const QString& previousFileId);
	void DownloadRecord(const CloudRevision& revision, std::function<void(std::optional<AuthenticatorRecord>)> done);
	[[nodiscard]] std::optional<QByteArray> Sealed(const QString& recordId, int revision, const AuthenticatorRecord& record) const;
	[[nodiscard]] DriveProperties RecordProperties(const QString& recordId, int revision) const;
	void SetStatus(CloudSyncStatus newStatus);

	GoogleConnection& googleConnection;
	AccountLibrary& accountLibrary;
	QTimer intervalTimer;
	QTimer changeTimer;
	std::deque<Step> pendingSteps;
	std::deque<Step> requestedSteps;
	QSet<QUuid> localRecordsInAction;
	QSet<QString> cloudRecordsInAction;
	std::vector<CloudConflict> conflicts;
	CloudSyncStatus status = CloudSyncStatus::INACTIVE;
	QDateTime lastSyncedAt;
	bool isSyncing = false;
	bool isRunReachable = true;
	bool isRunSaved = true;
	bool isRunReadable = true;
	bool isResyncPending = false;
	bool isApplyingCloud = false;
};

} // namespace SteamForge
