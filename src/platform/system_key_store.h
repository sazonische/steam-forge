#pragma once

#include "crypto/data_sealing.h"

namespace SteamForge {

enum class KeyStoreFailure {
	// No key store on this system, such as Linux without a Secret Service keyring.
	UNAVAILABLE,
	// The user dismissed the prompt of the keyring or of Windows Hello.
	DISMISSED,
	// The protection that asks the user is missing or cannot be enforced: Windows Hello without a PIN, face or
	// fingerprint, or a keyring that cannot make a separate keyring and keep it locked.
	NOT_SET_UP,
	NOT_FOUND,
	// Present, but not for this user account or computer, or damaged.
	UNREADABLE,
	FAILED
};

enum class KeyProtection {
	// Opens for the signed-in user account without asking.
	USER_ACCOUNT,
	// Asks for the Windows Hello PIN, face or fingerprint each time.
	WINDOWS_HELLO,
	// Kept in a separate keyring that stays locked, so the system asks for its password each time.
	KEYRING_PASSWORD
};

// Keeps the data key where the system gives it only to this user: Windows data protection (DPAPI) or a Windows
// Hello key, or the Secret Service keyring on Linux. The vault ID tells the keys of several data folders apart.
class SystemKeyStore {
public:
	SystemKeyStore() = default;
	virtual ~SystemKeyStore() = default;
	Q_DISABLE_COPY_MOVE(SystemKeyStore)

	[[nodiscard]] virtual bool IsAvailable() = 0;
	// The protection that asks the user each time, offered when encryption is turned on.
	[[nodiscard]] virtual KeyProtection PreferredProtection() const {
		return KeyProtection::USER_ACCOUNT;
	}
	// The window a system prompt belongs to.
	virtual void SetPromptWindow(WId /*window*/) {
	}
	[[nodiscard]] virtual std::expected<void, KeyStoreFailure> Save(const QString& vaultId, const DataKey& key, KeyProtection protection) = 0;
	[[nodiscard]] virtual std::expected<DataKey, KeyStoreFailure> Load(const QString& vaultId, KeyProtection protection) = 0;
	[[nodiscard]] virtual std::expected<void, KeyStoreFailure> Remove(const QString& vaultId, KeyProtection protection) = 0;
};

// The key store of this system for the data folder.
[[nodiscard]] std::unique_ptr<SystemKeyStore> CreateSystemKeyStore(const QString& dataDirectoryPath);

} // namespace SteamForge
