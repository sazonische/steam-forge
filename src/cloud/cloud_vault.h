#pragma once

#include "crypto/data_sealing.h"

namespace SteamForge {

inline constexpr qsizetype RECOVERY_KEY_LENGTH = 20;
// Drive properties that mark the files of a vault among anything else in the application folder.
inline constexpr QLatin1StringView DRIVE_KIND_PROPERTY{"kind"};
inline constexpr QLatin1StringView DRIVE_VAULT_ID_PROPERTY{"vaultId"};
inline constexpr QLatin1StringView VAULT_FILE_KIND{"vault"};
inline constexpr QLatin1StringView RECORD_FILE_KIND{"record"};

// Opens the cloud vault on a computer that does not have its key yet. The user writes it down when Google is
// connected; 160 random bits, so it needs no slow password hashing.
class RecoveryKey final {
public:
	[[nodiscard]] static std::optional<RecoveryKey> Generate();
	// Accepts the key as typed: any case, with or without dashes and spaces, 0, 1 and 8 read as O, I and B.
	[[nodiscard]] static std::optional<RecoveryKey> Parse(QStringView typedKey);

	RecoveryKey(const RecoveryKey&) = default;
	RecoveryKey(RecoveryKey&&) = default;
	RecoveryKey& operator=(const RecoveryKey&) = default;
	RecoveryKey& operator=(RecoveryKey&&) = default;
	~RecoveryKey();

	// Eight groups of four characters joined with dashes.
	[[nodiscard]] QString Text() const;
	[[nodiscard]] QByteArrayView Bytes() const;

private:
	RecoveryKey() = default;

	std::array<unsigned char, RECOVERY_KEY_LENGTH> bytes{};
};

// The cloud key wrapped with a key derived from the recovery key.
struct RecoveryKeySlot {
	QByteArray salt;
	SealedData wrappedKey;
};

// vault.json in the application folder of Google Drive. The cloud key encrypts every record of the vault; the
// header only holds it wrapped, so Google never sees it.
struct CloudVaultHeader {
	QString vaultId;
	QDateTime createdAt;
	RecoveryKeySlot recoverySlot;
};

enum class VaultHeaderError {
	NOT_A_VAULT,
	// Written by a newer SteamForge.
	NEWER_VERSION,
	DAMAGED
};

[[nodiscard]] std::optional<CloudVaultHeader> CreateVaultHeader(const QString& vaultId, const QDateTime& createdAt, const DataKey& cloudKey, const RecoveryKey& recoveryKey);
[[nodiscard]] QByteArray SerializeVaultHeader(const CloudVaultHeader& header);
[[nodiscard]] std::expected<CloudVaultHeader, VaultHeaderError> ParseVaultHeader(const QByteArray& headerJson);
// Nothing when the recovery key is not the one of this vault or the header was changed.
[[nodiscard]] std::optional<DataKey> OpenWithRecoveryKey(const CloudVaultHeader& header, const RecoveryKey& recoveryKey);

} // namespace SteamForge
