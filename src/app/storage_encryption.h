#pragma once

#include "platform/system_key_store.h"
#include "storage/storage_error.h"

namespace SteamForge {

class AccountLibrary;

enum class EncryptionStatus {
	OFF,
	ON,
	// The key asks the user first, with Windows Hello or the password of its keyring: the data opens after Unlock().
	LOCKED,
	// The files are sealed, but the system gives no key: another user account, another computer or a locked keyring.
	KEY_UNAVAILABLE,
	// The system has nowhere to keep the key, such as Linux without a Secret Service keyring.
	UNSUPPORTED
};

struct EncryptionFailure {
	std::optional<KeyStoreFailure> keyStoreFailure;
	std::optional<StorageError> storageError;
	// Refused before anything changed: the change would leave a file that does not open now unreadable for good.
	std::optional<StorageError> unreadableFile;
};

// Encryption of the account and sign-in files with a data key the system keeps for this user. Turning it on or off
// rewrites every file; a change cut short by a crash continues at the next start, since both formats are read.
class StorageEncryption final : public QObject {
	Q_OBJECT

public:
	StorageEncryption(SystemKeyStore& keyStore, AccountLibrary& library, QString stateFilePath, QObject* parent = nullptr);

	// Before AccountLibrary::Load(): hands the key of sealed files to the library. A key that asks the user waits
	// for Unlock() instead, and the library is not loaded until then.
	void PrepareLibrary();
	// Asks the user for the key; on success the library is loaded with it.
	[[nodiscard]] std::expected<void, EncryptionFailure> Unlock();
	// After the load: finishes a change the program did not complete.
	[[nodiscard]] std::expected<void, EncryptionFailure> FinishInterruptedChange();

	[[nodiscard]] EncryptionStatus Status() const;
	// While encryption is on and unlocked; the Google connection is kept under it as well.
	[[nodiscard]] const std::optional<DataKey>& CurrentKey() const;
	[[nodiscard]] KeyProtection Protection() const;
	[[nodiscard]] KeyProtection PreferredProtection() const;
	[[nodiscard]] std::expected<void, EncryptionFailure> Enable(KeyProtection protection);
	[[nodiscard]] std::expected<void, EncryptionFailure> Disable();

Q_SIGNALS:
	void StatusChanged();

private:
	enum class SavedState {
		NONE,
		ENABLING,
		ENABLED,
		DISABLING
	};

	[[nodiscard]] std::expected<void, StorageError> WriteState(SavedState state) const;
	[[nodiscard]] std::expected<void, EncryptionFailure> CompleteEnabling();
	[[nodiscard]] std::expected<void, EncryptionFailure> CompleteDisabling();
	void SetStatus(EncryptionStatus newStatus);

	SystemKeyStore& systemKeyStore;
	AccountLibrary& accountLibrary;
	QString filePath;
	SavedState savedState = SavedState::NONE;
	QString vaultId;
	KeyProtection keyProtection = KeyProtection::USER_ACCOUNT;
	std::optional<DataKey> dataKey;
	EncryptionStatus status = EncryptionStatus::OFF;
};

} // namespace SteamForge
