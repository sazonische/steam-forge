#include "app/storage_encryption.h"

#include "app/account_library.h"
#include "storage/private_file.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView FORMAT_NAME{"steamforge-encryption"};
	constexpr int FORMAT_VERSION = 1;
	constexpr QLatin1StringView FORMAT_KEY{"format"};
	constexpr QLatin1StringView VERSION_KEY{"formatVersion"};
	constexpr QLatin1StringView STATE_KEY{"state"};
	constexpr QLatin1StringView VAULT_ID_KEY{"vaultId"};
	constexpr QLatin1StringView PROTECTION_KEY{"protection"};
	constexpr QLatin1StringView WINDOWS_HELLO_PROTECTION{"windows-hello"};
	constexpr QLatin1StringView KEYRING_PASSWORD_PROTECTION{"keyring-password"};
	constexpr QLatin1StringView USER_ACCOUNT_PROTECTION{"user-account"};
	constexpr QLatin1StringView ENABLING_STATE{"enabling"};
	constexpr QLatin1StringView ENABLED_STATE{"enabled"};
	constexpr QLatin1StringView DISABLING_STATE{"disabling"};
	constexpr qint64 MAX_STATE_FILE_SIZE = qint64{64} << 10;

	EncryptionFailure KeyStoreError(KeyStoreFailure failure) {
		return {
			.keyStoreFailure = failure,
			.storageError = std::nullopt,
			.unreadableFile = std::nullopt,
		};
	}

	EncryptionFailure StorageFailure(const StorageError& error) {
		return {
			.keyStoreFailure = std::nullopt,
			.storageError = error,
			.unreadableFile = std::nullopt,
		};
	}

	EncryptionFailure UnreadableFileFailure(const StorageError& error) {
		return {
			.keyStoreFailure = std::nullopt,
			.storageError = std::nullopt,
			.unreadableFile = error,
		};
	}

	bool IsSealedWithAnotherKey(const StorageError& problem) {
		return problem.kind == StorageErrorKind::KEY_UNAVAILABLE || problem.kind == StorageErrorKind::UNSEALING_FAILED;
	}

	KeyProtection ParseProtection(const QString& name) {
		if (name == WINDOWS_HELLO_PROTECTION) {
			return KeyProtection::WINDOWS_HELLO;
		}
		if (name == KEYRING_PASSWORD_PROTECTION) {
			return KeyProtection::KEYRING_PASSWORD;
		}
		// Files written before Windows Hello existed have no protection and use the user account.
		return KeyProtection::USER_ACCOUNT;
	}

	QLatin1StringView ProtectionName(KeyProtection protection) {
		switch (protection) {
			case KeyProtection::WINDOWS_HELLO:
				return WINDOWS_HELLO_PROTECTION;
			case KeyProtection::KEYRING_PASSWORD:
				return KEYRING_PASSWORD_PROTECTION;
			case KeyProtection::USER_ACCOUNT:
				break;
		}
		return USER_ACCOUNT_PROTECTION;
	}

	// Such a key is read only after the user is asked.
	bool AsksUser(KeyProtection protection) {
		return protection != KeyProtection::USER_ACCOUNT;
	}
} // namespace

StorageEncryption::StorageEncryption(SystemKeyStore& keyStore, AccountLibrary& library, QString stateFilePath, QObject* parent) :
	QObject(parent),
	systemKeyStore(keyStore),
	accountLibrary(library),
	filePath(std::move(stateFilePath)) {
}

void StorageEncryption::PrepareLibrary() {
	const QFileInfo stateFile(filePath);
	if (stateFile.exists()) {
		const std::expected<QJsonObject, StorageError> document = ReadJsonObjectFile(stateFile, MAX_STATE_FILE_SIZE);
		const QString state = document.has_value() && document->value(FORMAT_KEY).toString() == FORMAT_NAME ? document->value(STATE_KEY).toString() : QString();
		vaultId = document.has_value() ? document->value(VAULT_ID_KEY).toString() : QString();
		keyProtection = document.has_value() ? ParseProtection(document->value(PROTECTION_KEY).toString()) : KeyProtection::USER_ACCOUNT;
		if (state == ENABLING_STATE) {
			savedState = SavedState::ENABLING;
		} else if (state == DISABLING_STATE) {
			savedState = SavedState::DISABLING;
		} else {
			// A damaged state file must not read as "off": the sealed files would then look lost for good.
			savedState = SavedState::ENABLED;
		}
	}
	if (savedState == SavedState::NONE) {
		status = systemKeyStore.IsAvailable() ? EncryptionStatus::OFF : EncryptionStatus::UNSUPPORTED;
		return;
	}
	if (AsksUser(keyProtection)) {
		status = EncryptionStatus::LOCKED;
		return;
	}
	std::expected<DataKey, KeyStoreFailure> key = systemKeyStore.Load(vaultId, keyProtection);
	if (!key.has_value()) {
		status = EncryptionStatus::KEY_UNAVAILABLE;
		return;
	}
	dataKey = std::move(*key);
	accountLibrary.SetDataKey(dataKey);
	status = EncryptionStatus::ON;
}

std::expected<void, EncryptionFailure> StorageEncryption::Unlock() {
	if (status != EncryptionStatus::LOCKED) {
		return {};
	}
	std::expected<DataKey, KeyStoreFailure> key = systemKeyStore.Load(vaultId, keyProtection);
	if (!key.has_value()) {
		// A dismissed prompt can be tried again; anything else means the key is gone for this account.
		if (key.error() != KeyStoreFailure::DISMISSED) {
			accountLibrary.Load();
			SetStatus(EncryptionStatus::KEY_UNAVAILABLE);
		}
		return std::unexpected(KeyStoreError(key.error()));
	}
	dataKey = std::move(*key);
	accountLibrary.SetDataKey(dataKey);
	accountLibrary.Load();
	SetStatus(EncryptionStatus::ON);
	return FinishInterruptedChange();
}

std::expected<void, EncryptionFailure> StorageEncryption::FinishInterruptedChange() {
	if (!dataKey.has_value()) {
		return {};
	}
	if (savedState == SavedState::ENABLING) {
		return CompleteEnabling();
	}
	if (savedState == SavedState::DISABLING) {
		return CompleteDisabling();
	}
	return {};
}

const std::optional<DataKey>& StorageEncryption::CurrentKey() const {
	return dataKey;
}

EncryptionStatus StorageEncryption::Status() const {
	return status;
}

KeyProtection StorageEncryption::Protection() const {
	return keyProtection;
}

KeyProtection StorageEncryption::PreferredProtection() const {
	return systemKeyStore.PreferredProtection();
}

std::expected<void, EncryptionFailure> StorageEncryption::Enable(KeyProtection protection) {
	if (status != EncryptionStatus::OFF) {
		return std::unexpected(KeyStoreError(status == EncryptionStatus::UNSUPPORTED ? KeyStoreFailure::UNAVAILABLE : KeyStoreFailure::FAILED));
	}
	// Sealed files that do not open belong to a key the state no longer names, such as after encryption.json was
	// deleted. A new key would replace the stored one, and they would stay closed for good.
	if (const auto sealedFile = std::ranges::find_if(accountLibrary.LoadProblems(), IsSealedWithAnotherKey); sealedFile != accountLibrary.LoadProblems().end()) {
		return std::unexpected(UnreadableFileFailure(*sealedFile));
	}
	std::optional<DataKey> newKey = DataKey::Generate();
	if (!newKey.has_value()) {
		return std::unexpected(KeyStoreError(KeyStoreFailure::FAILED));
	}
	const QString newVaultId = QUuid::createUuid().toString(QUuid::WithoutBraces);
	// The key is kept by the system before any file depends on it.
	if (const std::expected<void, KeyStoreFailure> saved = systemKeyStore.Save(newVaultId, *newKey, protection); !saved.has_value()) {
		return std::unexpected(KeyStoreError(saved.error()));
	}
	vaultId = newVaultId;
	keyProtection = protection;
	if (const std::expected<void, StorageError> written = WriteState(SavedState::ENABLING); !written.has_value()) {
		if (!systemKeyStore.Remove(vaultId, keyProtection).has_value()) {
			qWarning("The unused data key could not be removed from the system key store.");
		}
		vaultId.clear();
		return std::unexpected(StorageFailure(written.error()));
	}
	savedState = SavedState::ENABLING;
	dataKey = std::move(newKey);
	accountLibrary.SetDataKey(dataKey);
	SetStatus(EncryptionStatus::ON);
	return CompleteEnabling();
}

std::expected<void, EncryptionFailure> StorageEncryption::Disable() {
	if (status != EncryptionStatus::ON || !dataKey.has_value()) {
		return std::unexpected(KeyStoreError(KeyStoreFailure::FAILED));
	}
	// Files that could not be read stay sealed: removing the key would lose them for good.
	if (!accountLibrary.LoadProblems().empty()) {
		return std::unexpected(UnreadableFileFailure(accountLibrary.LoadProblems().front()));
	}
	// Turning the protection off is confirmed like opening the data.
	if (AsksUser(keyProtection)) {
		if (const std::expected<DataKey, KeyStoreFailure> confirmed = systemKeyStore.Load(vaultId, keyProtection); !confirmed.has_value()) {
			return std::unexpected(KeyStoreError(confirmed.error()));
		}
	}
	if (const std::expected<void, StorageError> written = WriteState(SavedState::DISABLING); !written.has_value()) {
		return std::unexpected(StorageFailure(written.error()));
	}
	savedState = SavedState::DISABLING;
	return CompleteDisabling();
}

std::expected<void, StorageError> StorageEncryption::WriteState(SavedState state) const {
	QLatin1StringView stateName = ENABLED_STATE;
	if (state == SavedState::ENABLING) {
		stateName = ENABLING_STATE;
	} else if (state == SavedState::DISABLING) {
		stateName = DISABLING_STATE;
	}
	const QJsonObject document{
		{FORMAT_KEY, FORMAT_NAME},
		{VERSION_KEY, FORMAT_VERSION},
		{STATE_KEY, stateName},
		{VAULT_ID_KEY, vaultId},
		{PROTECTION_KEY, ProtectionName(keyProtection)},
	};
	return WritePrivateFile(filePath, QJsonDocument(document).toJson(QJsonDocument::Indented));
}

std::expected<void, EncryptionFailure> StorageEncryption::CompleteEnabling() {
	// On a failure the state stays "enabling" and the next start seals the rest; both formats open meanwhile.
	if (const std::expected<void, StorageError> rewritten = accountLibrary.RewriteFiles(); !rewritten.has_value()) {
		return std::unexpected(StorageFailure(rewritten.error()));
	}
	if (const std::expected<void, StorageError> written = WriteState(SavedState::ENABLED); !written.has_value()) {
		return std::unexpected(StorageFailure(written.error()));
	}
	savedState = SavedState::ENABLED;
	return {};
}

std::expected<void, EncryptionFailure> StorageEncryption::CompleteDisabling() {
	accountLibrary.SetDataKey(std::nullopt);
	if (const std::expected<void, StorageError> rewritten = accountLibrary.RewriteFiles(); !rewritten.has_value()) {
		// Some files are plain now and the rest still sealed: the key stays until the next try.
		accountLibrary.SetDataKey(dataKey);
		return std::unexpected(StorageFailure(rewritten.error()));
	}
	if (const std::expected<void, StorageError> removed = RemoveFileIfExists(filePath); !removed.has_value()) {
		accountLibrary.SetDataKey(dataKey);
		return std::unexpected(StorageFailure(removed.error()));
	}
	if (!systemKeyStore.Remove(vaultId, keyProtection).has_value()) {
		// No file needs the key any more, so a key left behind opens nothing.
		qWarning("The data key could not be removed from the system key store.");
	}
	savedState = SavedState::NONE;
	vaultId.clear();
	keyProtection = KeyProtection::USER_ACCOUNT;
	dataKey.reset();
	SetStatus(systemKeyStore.IsAvailable() ? EncryptionStatus::OFF : EncryptionStatus::UNSUPPORTED);
	return {};
}

void StorageEncryption::SetStatus(EncryptionStatus newStatus) {
	if (status != newStatus) {
		status = newStatus;
		Q_EMIT StatusChanged();
	}
}

} // namespace SteamForge
