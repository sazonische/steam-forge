#include "app/cloud_sync.h"

#include "app/account_library.h"
#include "app/google_connection.h"
#include "cloud/cloud_record.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView RECORD_FILE_NAME{"record.json"};
	constexpr QLatin1StringView KIND_PROPERTY{"kind"};
	constexpr QLatin1StringView RECORD_KIND{"record"};
	constexpr QLatin1StringView VAULT_ID_PROPERTY{"vaultId"};
	constexpr QLatin1StringView RECORD_ID_PROPERTY{"recordId"};
	constexpr QLatin1StringView REVISION_PROPERTY{"revision"};
	constexpr QLatin1StringView PARENT_PROPERTY{"parent"};
	constexpr int FIRST_REVISION = 1;
	constexpr int NOT_FOUND_STATUS = 404;
	constexpr std::chrono::milliseconds SYNC_INTERVAL{std::chrono::minutes(10)};
	// Several quick edits of one account go up as one revision.
	constexpr std::chrono::milliseconds CHANGE_DELAY{std::chrono::seconds(3)};

	// A link that is not saved only repeats work: the next pass finds the same records again.
	void WarnIfNotSaved(const std::expected<void, StorageError>& saved) {
		if (!saved.has_value()) {
			qWarning("The Google sync state could not be saved; the next sync repeats the work.");
		}
	}
} // namespace

CloudSync::CloudSync(GoogleConnection& connection, AccountLibrary& library, QObject* parent) :
	QObject(parent),
	googleConnection(connection),
	accountLibrary(library) {
	intervalTimer.setInterval(SYNC_INTERVAL);
	changeTimer.setInterval(CHANGE_DELAY);
	changeTimer.setSingleShot(true);
	connect(&intervalTimer, &QTimer::timeout, this, &CloudSync::Synchronize);
	connect(&changeTimer, &QTimer::timeout, this, &CloudSync::Synchronize);
	connect(&library, &AccountLibrary::RecordsChanged, this, [this]() {
		// Writing a cloud version here is no local change to send back.
		if (!isApplyingCloud && status != CloudSyncStatus::INACTIVE) {
			changeTimer.start();
		}
	});
	connect(&connection, &GoogleConnection::StateChanged, this, [this]() {
		if (googleConnection.State() == GoogleConnectionState::CONNECTED) {
			Synchronize();
			return;
		}
		pendingSteps.clear();
		requestedSteps.clear();
		localRecordsInAction.clear();
		cloudRecordsInAction.clear();
		conflicts.clear();
		SetStatus(CloudSyncStatus::INACTIVE);
	});
}

void CloudSync::Start() {
	intervalTimer.start();
	Synchronize();
}

void CloudSync::Stop() {
	intervalTimer.stop();
	changeTimer.stop();
}

void CloudSync::Synchronize() {
	GoogleDrive* drive = googleConnection.Drive();
	if (drive == nullptr || !googleConnection.CloudKey().has_value()) {
		SetStatus(CloudSyncStatus::INACTIVE);
		return;
	}
	if (isSyncing) {
		isResyncPending = true;
		return;
	}
	isSyncing = true;
	isPassReachable = true;
	SetStatus(CloudSyncStatus::SYNCING);
	drive->ListFiles(*this, [this](std::expected<std::vector<DriveFile>, GoogleError> files) {
		if (!files.has_value()) {
			isPassReachable = false;
			FinishPass();
			return;
		}
		PlanPass(*files);
		RunNextStep();
	});
}

void CloudSync::AddToCloud(const QUuid& recordId) {
	// The action asked earlier reports for both.
	if (IsChanging(recordId)) {
		return;
	}
	const AuthenticatorRecord* record = accountLibrary.FindRecord(recordId);
	if (IsInCloud(recordId) || record == nullptr || !DetermineCapabilities(*record).canGenerateCodes || googleConnection.Drive() == nullptr) {
		Q_EMIT AddingFinished(recordId, IsInCloud(recordId));
		return;
	}
	Request(recordId, {}, [this, recordId]() {
		const AuthenticatorRecord* requestedRecord = accountLibrary.FindRecord(recordId);
		// A pass may have linked the same Steam Guard meanwhile or found another one of the account in the cloud.
		if (requestedRecord == nullptr || IsInCloud(recordId) || HasConflict(recordId)) {
			Q_EMIT AddingFinished(recordId, IsInCloud(recordId));
			RunNextStep();
			return;
		}
		UploadNewRecord(*requestedRecord, [this, recordId](bool isUploaded) {
			Q_EMIT AddingFinished(recordId, isUploaded);
			RunNextStep();
		});
	});
}

void CloudSync::RemoveFromCloud(const QUuid& recordId) {
	const std::vector<CloudLink>& links = googleConnection.Links();
	const auto link = std::ranges::find(links, recordId, &CloudLink::localRecordId);
	if (link == links.end() || IsChanging(recordId)) {
		return;
	}
	// Taken now: the account may leave this computer before the step runs.
	Request(recordId, link->cloudRecordId, [this, recordId, cloudRecordId = link->cloudRecordId]() {
		GoogleDrive* drive = googleConnection.Drive();
		if (drive == nullptr) {
			RunNextStep();
			return;
		}
		// Every revision goes, so no leftover file comes back as an account on the next pass.
		drive->ListFiles(*this, [this, recordId, cloudRecordId](std::expected<std::vector<DriveFile>, GoogleError> files) {
			if (!files.has_value()) {
				isPassReachable = false;
				RunNextStep();
				return;
			}
			std::vector<QString> recordFileIds;
			for (const DriveFile& file : *files) {
				if (file.appProperties.value(RECORD_ID_PROPERTY) == cloudRecordId) {
					recordFileIds.push_back(file.id);
				}
			}
			DeleteFiles(std::move(recordFileIds), [this, recordId, cloudRecordId](bool isAllDeleted) {
				// A file left behind would link the account again on the next pass.
				if (isAllDeleted) {
					WarnIfNotSaved(googleConnection.RemoveLink(recordId));
					WarnIfNotSaved(googleConnection.ForgetCloudOnlyRecords(QSet<QString>{cloudRecordId}));
				}
				RunNextStep();
			});
		});
	});
}

void CloudSync::ResolveConflict(const QString& cloudRecordId, ConflictChoice choice) {
	const auto found = std::ranges::find(conflicts, cloudRecordId, &CloudConflict::cloudRecordId);
	if (found == conflicts.end()) {
		return;
	}
	const CloudConflict conflict = std::move(*found);
	conflicts.erase(found);
	Request(conflict.localRecordId, conflict.cloudRecordId, [this, conflict, choice]() {
		ApplyDecision(conflict, choice);
	});
}

void CloudSync::LeaveInCloud(const QUuid& recordId, const QString& title) {
	if (const std::expected<void, StorageError> left = googleConnection.LeaveOnlyInCloud(recordId, title); !left.has_value()) {
		qWarning("The account removed from this computer may come back from Google: the choice could not be saved.");
	}
	Q_EMIT StatusChanged();
}

void CloudSync::ReturnFromCloud(const QString& cloudRecordId) {
	if (googleConnection.ForgetCloudOnlyRecords(QSet<QString>{cloudRecordId}).has_value()) {
		Synchronize();
	}
}

void CloudSync::ApplyDecision(const CloudConflict& conflict, ConflictChoice choice) {
	if (choice == ConflictChoice::KEEP_BOTH) {
		AuthenticatorRecord added = conflict.cloudRecord;
		added.id = QUuid::createUuid();
		isApplyingCloud = true;
		const std::expected<void, StorageError> saved = accountLibrary.Add(added);
		isApplyingCloud = false;
		if (saved.has_value()) {
			WarnIfNotSaved(googleConnection.AddLink(CloudLink{.localRecordId = added.id, .cloudRecordId = conflict.cloudRecordId, .revision = conflict.cloudRevision, .fileId = conflict.cloudFileId, .fingerprint = CloudRecordFingerprint(added)}));
			Q_EMIT RecordAdded(added.id);
		}
		RunNextStep();
		return;
	}
	const AuthenticatorRecord* local = accountLibrary.FindRecord(conflict.localRecordId);
	if (local == nullptr) {
		RunNextStep();
		return;
	}
	const CloudLink link{.localRecordId = local->id, .cloudRecordId = conflict.cloudRecordId, .revision = conflict.cloudRevision, .fileId = conflict.cloudFileId, .fingerprint = {}};
	pendingSteps.emplace_front([this, localRecordId = local->id]() {
		KeepOnlyLinkedRevision(localRecordId);
	});
	if (choice == ConflictChoice::USE_CLOUD) {
		const CloudRevision revision{.recordId = conflict.cloudRecordId, .revision = conflict.cloudRevision, .file = DriveFile{.id = conflict.cloudFileId, .name = {}, .appProperties = {}, .createdTime = {}}};
		ApplyCloudVersion(link, revision);
		return;
	}
	PushLocalVersion(link, *local, conflict.cloudRevision);
}

CloudSyncStatus CloudSync::Status() const {
	return status;
}

const QDateTime& CloudSync::LastSyncedAt() const {
	return lastSyncedAt;
}

const std::vector<CloudConflict>& CloudSync::Conflicts() const {
	return conflicts;
}

bool CloudSync::IsInCloud(const QUuid& recordId) const {
	return std::ranges::contains(googleConnection.Links(), recordId, &CloudLink::localRecordId);
}

bool CloudSync::HasConflict(const QUuid& recordId) const {
	return std::ranges::contains(conflicts, recordId, &CloudConflict::localRecordId);
}

bool CloudSync::IsChanging(const QUuid& recordId) const {
	return localRecordsInAction.contains(recordId);
}

void CloudSync::Request(const QUuid& localRecordId, const QString& cloudRecordId, Step step) {
	localRecordsInAction.insert(localRecordId);
	if (!cloudRecordId.isEmpty()) {
		cloudRecordsInAction.insert(cloudRecordId);
	}
	requestedSteps.push_back(std::move(step));
	requestedSteps.emplace_back([this, localRecordId, cloudRecordId]() {
		localRecordsInAction.remove(localRecordId);
		cloudRecordsInAction.remove(cloudRecordId);
		Q_EMIT StatusChanged();
		RunNextStep();
	});
	// The state that counts is the one a fresh pass finds after the action.
	isResyncPending = true;
	Q_EMIT StatusChanged();
	RunRequestedSteps();
}

void CloudSync::RunRequestedSteps() {
	if (isSyncing || requestedSteps.empty()) {
		return;
	}
	isSyncing = true;
	SetStatus(CloudSyncStatus::SYNCING);
	pendingSteps = std::exchange(requestedSteps, {});
	RunNextStep();
}

void CloudSync::PlanPass(const std::vector<DriveFile>& files) {
	const RevisionsByRecord revisionsByRecord = RecordRevisions(files);
	const QSet<QString> cloudOnlyRecordIds = CurrentCloudOnlyRecordIds(revisionsByRecord);
	conflicts.clear();
	pendingSteps.clear();
	QSet<QString> linkedRecordIds;
	for (const CloudLink& link : googleConnection.Links()) {
		linkedRecordIds.insert(link.cloudRecordId);
		if (!localRecordsInAction.contains(link.localRecordId) && !cloudRecordsInAction.contains(link.cloudRecordId)) {
			PlanLink(link, revisionsByRecord);
		}
	}
	for (const auto& [recordId, revisions] : revisionsByRecord) {
		if (!linkedRecordIds.contains(recordId) && !cloudRecordsInAction.contains(recordId) && !cloudOnlyRecordIds.contains(recordId)) {
			pendingSteps.emplace_back([this, newest = revisions.front()]() {
				AdoptCloudRecord(newest);
			});
		}
	}
}

CloudSync::RevisionsByRecord CloudSync::RecordRevisions(const std::vector<DriveFile>& files) const {
	RevisionsByRecord revisionsByRecord;
	for (const DriveFile& file : files) {
		bool isRevisionNumber = false;
		const int revision = file.appProperties.value(REVISION_PROPERTY).toInt(&isRevisionNumber);
		const QString recordId = file.appProperties.value(RECORD_ID_PROPERTY);
		if (file.appProperties.value(KIND_PROPERTY) != RECORD_KIND || file.appProperties.value(VAULT_ID_PROPERTY) != googleConnection.VaultId() || !isRevisionNumber || recordId.isEmpty()) {
			continue;
		}
		revisionsByRecord[recordId].push_back(CloudRevision{.recordId = recordId, .revision = revision, .file = file});
	}
	for (auto& [recordId, revisions] : revisionsByRecord) {
		std::ranges::sort(revisions, std::ranges::greater(), &CloudRevision::revision);
	}
	return revisionsByRecord;
}

QSet<QString> CloudSync::CurrentCloudOnlyRecordIds(const RevisionsByRecord& revisionsByRecord) {
	QSet<QString> cloudOnlyRecordIds;
	QSet<QString> vanishedRecordIds;
	for (const CloudOnlyRecord& record : googleConnection.CloudOnlyRecords()) {
		if (revisionsByRecord.contains(record.cloudRecordId)) {
			cloudOnlyRecordIds.insert(record.cloudRecordId);
		} else {
			vanishedRecordIds.insert(record.cloudRecordId);
		}
	}
	if (!vanishedRecordIds.isEmpty()) {
		WarnIfNotSaved(googleConnection.ForgetCloudOnlyRecords(vanishedRecordIds));
	}
	return cloudOnlyRecordIds;
}

void CloudSync::PlanLink(const CloudLink& link, const RevisionsByRecord& revisionsByRecord) {
	const AuthenticatorRecord* local = accountLibrary.FindRecord(link.localRecordId);
	if (local == nullptr) {
		// Removed from SteamForge here: the cloud record stays for the other computers.
		pendingSteps.emplace_back([this, localRecordId = link.localRecordId]() {
			WarnIfNotSaved(googleConnection.RemoveLink(localRecordId));
			RunNextStep();
		});
		return;
	}
	const auto found = revisionsByRecord.find(link.cloudRecordId);
	if (found == revisionsByRecord.end()) {
		pendingSteps.emplace_back([this, localRecordId = link.localRecordId]() {
			if (googleConnection.RemoveLink(localRecordId).has_value()) {
				Q_EMIT RecordUnlinked(localRecordId);
			}
			RunNextStep();
		});
		return;
	}
	const std::vector<CloudRevision>& revisions = found->second;
	const CloudRevision& latest = revisions.front();
	const bool isLocalChanged = CloudRecordFingerprint(*local) != link.fingerprint;
	// Two computers sent the same next revision.
	if (revisions.size() > 1 && revisions.at(1).revision == latest.revision) {
		pendingSteps.emplace_back([this, localRecordId = link.localRecordId, theirs = latest.file.id == link.fileId ? revisions.at(1) : latest]() {
			CollectConflict(CloudConflictKind::CHANGED_ON_BOTH, localRecordId, theirs);
		});
		return;
	}
	if (latest.revision > link.revision && isLocalChanged) {
		pendingSteps.emplace_back([this, localRecordId = link.localRecordId, latest]() {
			CollectConflict(CloudConflictKind::CHANGED_ON_BOTH, localRecordId, latest);
		});
		return;
	}
	if (latest.revision > link.revision) {
		pendingSteps.emplace_back([this, link, latest]() {
			ApplyCloudVersion(link, latest);
		});
		return;
	}
	if (isLocalChanged) {
		pendingSteps.emplace_back([this, link, record = *local, baseRevision = latest.revision]() {
			PushLocalVersion(link, record, baseRevision);
		});
	}
}

void CloudSync::DeleteFiles(std::vector<QString> fileIds, std::function<void(bool)> done) {
	if (fileIds.empty()) {
		done(true);
		return;
	}
	GoogleDrive* drive = googleConnection.Drive();
	if (drive == nullptr) {
		done(false);
		return;
	}
	const QString fileId = fileIds.back();
	fileIds.pop_back();
	drive->Delete(fileId, *this, [this, remainingIds = std::move(fileIds), deletionDone = std::move(done)](const std::expected<void, GoogleError>& deleted) mutable {
		if (!deleted.has_value() && deleted.error().httpStatus != NOT_FOUND_STATUS) {
			isPassReachable = false;
			deletionDone(false);
			return;
		}
		DeleteFiles(std::move(remainingIds), std::move(deletionDone));
	});
}

void CloudSync::KeepOnlyLinkedRevision(const QUuid& localRecordId) {
	const std::vector<CloudLink>& links = googleConnection.Links();
	const auto link = std::ranges::find(links, localRecordId, &CloudLink::localRecordId);
	GoogleDrive* drive = googleConnection.Drive();
	if (link == links.end() || drive == nullptr) {
		RunNextStep();
		return;
	}
	drive->ListFiles(*this, [this, cloudRecordId = link->cloudRecordId, keptFileId = link->fileId](std::expected<std::vector<DriveFile>, GoogleError> files) {
		if (!files.has_value()) {
			RunNextStep();
			return;
		}
		std::vector<QString> otherFileIds;
		for (const DriveFile& file : *files) {
			if (file.appProperties.value(RECORD_ID_PROPERTY) == cloudRecordId && file.id != keptFileId) {
				otherFileIds.push_back(file.id);
			}
		}
		DeleteFiles(std::move(otherFileIds), [this](bool) {
			RunNextStep();
		});
	});
}

void CloudSync::RunNextStep() {
	if (pendingSteps.empty()) {
		FinishPass();
		return;
	}
	const Step step = std::move(pendingSteps.front());
	pendingSteps.pop_front();
	step();
}

void CloudSync::FinishPass() {
	if (!isSyncing) {
		return;
	}
	isSyncing = false;
	if (googleConnection.Drive() == nullptr) {
		SetStatus(CloudSyncStatus::INACTIVE);
		return;
	}
	if (!requestedSteps.empty()) {
		RunRequestedSteps();
		return;
	}
	if (isResyncPending) {
		isResyncPending = false;
		Synchronize();
		return;
	}
	if (!isPassReachable) {
		SetStatus(CloudSyncStatus::UNREACHABLE);
		return;
	}
	lastSyncedAt = QDateTime::currentDateTime();
	SetStatus(conflicts.empty() ? CloudSyncStatus::SYNCED : CloudSyncStatus::NEEDS_DECISION);
	// The time of the last sync changes even when the state does not.
	Q_EMIT StatusChanged();
}

void CloudSync::ApplyCloudVersion(CloudLink link, const CloudRevision& revision) {
	DownloadRecord(revision, [this, link, revision](std::optional<AuthenticatorRecord> cloudRecord) mutable {
		const AuthenticatorRecord* local = accountLibrary.FindRecord(link.localRecordId);
		if (!cloudRecord.has_value() || local == nullptr) {
			RunNextStep();
			return;
		}
		cloudRecord->id = link.localRecordId;
		const AuthenticatorRecord applied = WithLocalSteamSignIn(std::move(*cloudRecord), *local);
		// No Steam Guard key leaves this computer without a copy.
		const bool isKeyReplaced = applied.secrets.sharedSecret != local->secrets.sharedSecret;
		if (isKeyReplaced && !accountLibrary.SaveBackup(*local).has_value()) {
			RunNextStep();
			return;
		}
		isApplyingCloud = true;
		const std::expected<void, StorageError> saved = accountLibrary.Save(applied);
		isApplyingCloud = false;
		if (saved.has_value()) {
			link.revision = revision.revision;
			link.fileId = revision.file.id;
			link.fingerprint = CloudRecordFingerprint(applied);
			WarnIfNotSaved(googleConnection.AddLink(link));
			if (isKeyReplaced) {
				Q_EMIT KeyReplaced(link.localRecordId);
			}
		}
		RunNextStep();
	});
}

void CloudSync::PushLocalVersion(CloudLink link, const AuthenticatorRecord& record, int baseRevision) {
	GoogleDrive* drive = googleConnection.Drive();
	const int revision = baseRevision + 1;
	const std::optional<QByteArray> content = Sealed(link.cloudRecordId, revision, record);
	if (drive == nullptr || !content.has_value()) {
		RunNextStep();
		return;
	}
	DriveProperties properties = RecordProperties(link.cloudRecordId, revision);
	properties.insert(PARENT_PROPERTY, link.fileId);
	drive->Create(RECORD_FILE_NAME, properties, *content, *this, [this, link, revision, fingerprint = CloudRecordFingerprint(record)](std::expected<DriveFile, GoogleError> uploaded) mutable {
		GoogleDrive* uploadingDrive = googleConnection.Drive();
		if (!uploaded.has_value() || uploadingDrive == nullptr) {
			isPassReachable = false;
			RunNextStep();
			return;
		}
		const QString previousFileId = link.fileId;
		link.revision = revision;
		link.fileId = uploaded->id;
		link.fingerprint = fingerprint;
		if (googleConnection.AddLink(link).has_value()) {
			// The new revision is in place, so the previous one is no longer needed.
			uploadingDrive->Delete(previousFileId, *this, [](const std::expected<void, GoogleError>&) {
			});
		}
		RunNextStep();
	});
}

void CloudSync::AdoptCloudRecord(const CloudRevision& revision) {
	DownloadRecord(revision, [this, revision](std::optional<AuthenticatorRecord> cloudRecord) {
		if (!cloudRecord.has_value()) {
			RunNextStep();
			return;
		}
		const std::vector<AuthenticatorRecord>& records = accountLibrary.Records();
		const auto sameKey = std::ranges::find_if(records, [&cloudRecord](const AuthenticatorRecord& record) {
			return !record.secrets.sharedSecret.isEmpty() && record.secrets.sharedSecret == cloudRecord->secrets.sharedSecret;
		});
		if (sameKey != records.end()) {
			if (!IsInCloud(sameKey->id)) {
				LinkSameGuard(*sameKey, *cloudRecord, revision);
			}
			RunNextStep();
			return;
		}
		const auto sameAccount = std::ranges::find_if(records, [&cloudRecord](const AuthenticatorRecord& record) {
			return record.account.steamId.has_value() && record.account.steamId == cloudRecord->account.steamId;
		});
		if (sameAccount != records.end() && !IsInCloud(sameAccount->id)) {
			conflicts.push_back(CloudConflict{.kind = CloudConflictKind::OTHER_KEY_OF_ACCOUNT, .localRecordId = sameAccount->id, .cloudRecord = std::move(*cloudRecord), .cloudRecordId = revision.recordId, .cloudRevision = revision.revision, .cloudFileId = revision.file.id});
			RunNextStep();
			return;
		}
		AuthenticatorRecord added = std::move(*cloudRecord);
		added.id = QUuid::createUuid();
		isApplyingCloud = true;
		const std::expected<void, StorageError> saved = accountLibrary.Add(added);
		isApplyingCloud = false;
		if (saved.has_value()) {
			WarnIfNotSaved(googleConnection.AddLink(CloudLink{.localRecordId = added.id, .cloudRecordId = revision.recordId, .revision = revision.revision, .fileId = revision.file.id, .fingerprint = CloudRecordFingerprint(added)}));
			Q_EMIT RecordAdded(added.id);
		}
		RunNextStep();
	});
}

// The same Steam Guard is one record: they are linked, and fields one side lacks come from the other.
void CloudSync::LinkSameGuard(AuthenticatorRecord local, const AuthenticatorRecord& cloudRecord, const CloudRevision& revision) {
	if (const std::optional<AuthenticatorRecord> completed = CompleteRecord(local, cloudRecord); completed.has_value()) {
		isApplyingCloud = true;
		if (accountLibrary.Save(*completed).has_value()) {
			local = *completed;
		}
		isApplyingCloud = false;
	}
	AuthenticatorRecord cloudAsLocal = cloudRecord;
	cloudAsLocal.id = local.id;
	const QString cloudFingerprint = CloudRecordFingerprint(cloudAsLocal);
	WarnIfNotSaved(googleConnection.AddLink(CloudLink{.localRecordId = local.id, .cloudRecordId = revision.recordId, .revision = revision.revision, .fileId = revision.file.id, .fingerprint = cloudFingerprint}));
	// What this computer adds goes up on the next pass.
	if (CloudRecordFingerprint(local) != cloudFingerprint) {
		isResyncPending = true;
	}
}

void CloudSync::CollectConflict(CloudConflictKind kind, const QUuid& localRecordId, const CloudRevision& revision) {
	DownloadRecord(revision, [this, kind, localRecordId, revision](std::optional<AuthenticatorRecord> cloudRecord) {
		if (cloudRecord.has_value()) {
			conflicts.push_back(CloudConflict{.kind = kind, .localRecordId = localRecordId, .cloudRecord = std::move(*cloudRecord), .cloudRecordId = revision.recordId, .cloudRevision = revision.revision, .cloudFileId = revision.file.id});
		}
		RunNextStep();
	});
}

void CloudSync::UploadNewRecord(const AuthenticatorRecord& record, std::function<void(bool)> done) {
	GoogleDrive* drive = googleConnection.Drive();
	const QString recordId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const std::optional<QByteArray> content = Sealed(recordId, FIRST_REVISION, record);
	if (drive == nullptr || !content.has_value()) {
		done(false);
		return;
	}
	drive->Create(RECORD_FILE_NAME, RecordProperties(recordId, FIRST_REVISION), *content, *this, [this, recordId, localRecordId = record.id, fingerprint = CloudRecordFingerprint(record), uploadDone = std::move(done)](std::expected<DriveFile, GoogleError> uploaded) {
		if (!uploaded.has_value()) {
			isPassReachable = false;
			uploadDone(false);
			return;
		}
		const CloudLink link{.localRecordId = localRecordId, .cloudRecordId = recordId, .revision = FIRST_REVISION, .fileId = uploaded->id, .fingerprint = fingerprint};
		uploadDone(googleConnection.AddLink(link).has_value());
	});
}

void CloudSync::DownloadRecord(const CloudRevision& revision, std::function<void(std::optional<AuthenticatorRecord>)> done) {
	GoogleDrive* drive = googleConnection.Drive();
	if (drive == nullptr) {
		done(std::nullopt);
		return;
	}
	drive->Download(revision.file.id, *this, [this, revision, downloadDone = std::move(done)](std::expected<QByteArray, GoogleError> content) {
		const std::optional<DataKey>& cloudKey = googleConnection.CloudKey();
		if (!content.has_value() || !cloudKey.has_value()) {
			downloadDone(std::nullopt);
			return;
		}
		const CloudRecordPlace place{.vaultId = googleConnection.VaultId(), .recordId = revision.recordId, .revision = revision.revision};
		std::expected<AuthenticatorRecord, CloudRecordError> record = OpenCloudRecord(*cloudKey, place, *content);
		if (!record.has_value()) {
			downloadDone(std::nullopt);
			return;
		}
		downloadDone(std::move(*record));
	});
}

std::optional<QByteArray> CloudSync::Sealed(const QString& recordId, int revision, const AuthenticatorRecord& record) const {
	const std::optional<DataKey>& cloudKey = googleConnection.CloudKey();
	if (!cloudKey.has_value()) {
		return std::nullopt;
	}
	return SealCloudRecord(*cloudKey, CloudRecordPlace{.vaultId = googleConnection.VaultId(), .recordId = recordId, .revision = revision}, record);
}

DriveProperties CloudSync::RecordProperties(const QString& recordId, int revision) const {
	return DriveProperties{
		{KIND_PROPERTY, RECORD_KIND},
		{VAULT_ID_PROPERTY, googleConnection.VaultId()},
		{RECORD_ID_PROPERTY, recordId},
		{REVISION_PROPERTY, QString::number(revision)},
	};
}

void CloudSync::SetStatus(CloudSyncStatus newStatus) {
	if (status == newStatus) {
		return;
	}
	status = newStatus;
	Q_EMIT StatusChanged();
}

} // namespace SteamForge
