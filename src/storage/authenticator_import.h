#pragma once

#include "domain/authenticator_record.h"

namespace SteamForge {

enum class ImportErrorKind {
	FILE_UNREADABLE,
	FILE_TOO_LARGE,
	UNRECOGNIZED_FORMAT,
	INVALID_CONTENT,
	INCONSISTENT_SECRETS,
	SDA_MANIFEST_SELECTED,
	SDA_MANIFEST_MISSING,
	SDA_MANIFEST_ENTRY_MISSING,
	PASSKEY_REQUIRED,
	PASSKEY_INCORRECT
};

struct ImportError {
	ImportErrorKind kind = ImportErrorKind::INVALID_CONTENT;
	// Name of the offending field, line number or system error text.
	QString detail;
};

struct ImportResult {
	std::vector<AuthenticatorRecord> records;
	// Entries that are not Steam authenticators, such as links of other services.
	int skippedEntryCount = 0;
};

// Steam Desktop Authenticator keeps the PBKDF2 salt and the AES IV of each encrypted .maFile in manifest.json.
struct SdaEncryptionParameters {
	QByteArray salt;
	QByteArray initializationVector;
};

[[nodiscard]] std::expected<ImportResult, ImportError> ParseMaFile(const QByteArray& fileContents, const QString& fileName, RecordOrigin origin = RecordOrigin::MA_FILE);
[[nodiscard]] std::expected<ImportResult, ImportError> ParseOtpauthLinks(QStringView text, const QString& fileName);

[[nodiscard]] std::expected<SdaEncryptionParameters, ImportError> FindSdaEncryptionParameters(const QByteArray& manifestContents, const QString& maFileName);
[[nodiscard]] std::expected<QByteArray, ImportError> DecryptSdaMaFile(const QByteArray& encryptedContents, const SdaEncryptionParameters& parameters, const QString& passkey);

// An encrypted .maFile needs the manifest.json of its folder and the passkey.
[[nodiscard]] std::expected<ImportResult, ImportError> ImportFile(const QString& filePath, const std::optional<QString>& passkey);

} // namespace SteamForge
