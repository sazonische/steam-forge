#pragma once

#include "cloud/cloud_vault.h"
#include "cloud/google_drive.h"
#include "storage/storage_error.h"

namespace SteamForge {

class GoogleRedirectReceiver;
class StorageEncryption;

enum class GoogleConnectionState {
	// This build has no OAuth client of SteamForge.
	UNAVAILABLE,
	DISCONNECTED,
	// The local data is encrypted and still locked; the saved connection opens together with it.
	LOCKED,
	CONNECTING,
	CONNECTED,
	// Google no longer accepts the saved sign-in. The user signs in again; the key of the vault stays.
	SIGN_IN_REQUIRED
};

enum class GoogleConnectFailure {
	ENCRYPTION_OFF,
	// No free port on 127.0.0.1 for the answer of the sign-in page.
	RECEIVER_UNAVAILABLE,
	SIGN_IN_CANCELLED,
	DRIVE_PERMISSION_MISSING,
	GOOGLE_UNREACHABLE,
	// Signed in again to a Google account whose vault is not the one this computer uses.
	OTHER_VAULT,
	VAULT_NEWER_VERSION,
	VAULT_DAMAGED,
	KEY_GENERATION_FAILED,
	SAVE_FAILED
};

// A local record and its record in the cloud vault.
struct CloudLink {
	QUuid localRecordId;
	QString cloudRecordId;
	int revision = 1;
	QString fileId;
	// The local record as it was when last synced; a different one means a local change to upload.
	QString fingerprint;
};

// A cloud record the user removed from this computer only: passes do not bring it back until the user returns it.
struct CloudOnlyRecord {
	QString cloudRecordId;
	// The name the account had here, shown while it is away.
	QString title;
};

// The Google account this computer syncs with: the sign-in, the hidden folder on Drive and the key of the cloud
// vault. The connection is kept in google.json, sealed with the local data key, so Google needs local encryption.
class GoogleConnection final : public QObject {
	Q_OBJECT

public:
	GoogleConnection(GoogleTransport& transport, StorageEncryption& encryption, GoogleClient client, GoogleEndpoints endpoints, QString stateFilePath, QObject* parent = nullptr);
	~GoogleConnection() override;
	Q_DISABLE_COPY_MOVE(GoogleConnection)

	[[nodiscard]] GoogleConnectionState State() const;
	[[nodiscard]] const QString& Email() const;
	// Also while it is locked or Google asks to sign in again: local encryption has to stay on.
	[[nodiscard]] bool HasSavedConnection() const;
	// Reads the saved connection once the local data key is available.
	void Load();

	// Starts signing in; SignInPageReady gives the page to open in the browser.
	void Connect();
	void CancelConnecting();
	// After NewVaultCreated: the user saved the recovery key, so the vault goes to Google Drive.
	void ConfirmRecoveryKeySaved();
	// After RecoveryKeyNeeded.
	void OpenVault(const RecoveryKey& recoveryKey);
	// After RecoveryKeyNeeded, when the key is lost: a new vault with a new key replaces the old one. The old files
	// are deleted only once the user saved the new key, and the accounts on the computer stay.
	void ReplaceVault();
	// Forgets the connection on this computer only: the cloud vault and the other computers stay as they are.
	[[nodiscard]] std::expected<void, StorageError> Disconnect();

	// While connected.
	[[nodiscard]] GoogleDrive* Drive();
	[[nodiscard]] const std::optional<DataKey>& CloudKey() const;
	[[nodiscard]] const QString& VaultId() const;
	[[nodiscard]] const std::vector<CloudLink>& Links() const;
	// Replaces the link of the same local record.
	[[nodiscard]] std::expected<void, StorageError> AddLink(const CloudLink& link);
	[[nodiscard]] std::expected<void, StorageError> RemoveLink(const QUuid& localRecordId);
	[[nodiscard]] const std::vector<CloudOnlyRecord>& CloudOnlyRecords() const;
	// The link of the local record becomes a cloud-only record in one save.
	[[nodiscard]] std::expected<void, StorageError> LeaveOnlyInCloud(const QUuid& localRecordId, const QString& title);
	[[nodiscard]] std::expected<void, StorageError> ForgetCloudOnlyRecords(const QSet<QString>& cloudRecordIds);

Q_SIGNALS:
	void StateChanged();
	void SignInPageReady(const QUrl& pageUrl);
	// The vault of this Google account is new: the user writes the key down before it is uploaded.
	void NewVaultCreated(const QString& recoveryKeyText);
	// The vault already exists and this computer does not have its key.
	void RecoveryKeyNeeded();
	void RecoveryKeyRejected();
	void Connected();
	void ConnectFailed(GoogleConnectFailure failure);
	// Links or cloud-only records.
	void LinksChanged();

private:
	struct SignInAttempt;

	void ExchangeCode(const QString& authorizationCode);
	void DeleteOldVault(std::vector<DriveFile> remainingFiles);
	void UploadVaultHeader();
	void FindVault();
	void CreateVault();
	void UseExistingVault(const DriveFile& vaultFile);
	void CheckUploadedVault(const DriveFile& uploadedFile);
	void FinishConnecting();
	void Fail(GoogleConnectFailure failure);
	void Fail(const GoogleError& error);
	void DiscardAttempt();
	// Answers of an attempt the user cancelled must not act on the next one.
	[[nodiscard]] bool IsCurrentAttempt(quint64 number) const;
	void SetState(GoogleConnectionState newState);
	struct SavedState {
		QString email;
		QString refreshToken;
		QString vaultId;
		QString vaultFileId;
		const DataKey* cloudKey = nullptr;
		const std::vector<CloudLink>* links = nullptr;
		const std::vector<CloudOnlyRecord>* cloudOnlyRecords = nullptr;
	};

	[[nodiscard]] std::expected<void, StorageError> SaveState(const SavedState& savedState) const;
	[[nodiscard]] std::expected<void, StorageError> SaveLinks(std::vector<CloudLink> changedLinks, std::vector<CloudOnlyRecord> changedCloudOnlyRecords);

	GoogleTransport& googleTransport;
	StorageEncryption& storageEncryption;
	GoogleClient googleClient;
	GoogleEndpoints googleEndpoints;
	QString filePath;
	GoogleConnectionState state = GoogleConnectionState::DISCONNECTED;
	QString email;
	QString vaultId;
	QString vaultFileId;
	std::optional<DataKey> cloudKey;
	std::vector<CloudLink> links;
	std::vector<CloudOnlyRecord> cloudOnlyRecords;
	std::unique_ptr<GoogleAccess> access;
	std::unique_ptr<GoogleDrive> drive;
	std::unique_ptr<SignInAttempt> attempt;
	quint64 attemptCount = 0;
};

} // namespace SteamForge
