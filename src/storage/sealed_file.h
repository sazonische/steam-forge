#pragma once

#include "crypto/data_sealing.h"
#include "storage/storage_error.h"

namespace SteamForge {

enum class SealedFileKind {
	ACCOUNT,
	SESSION,
	GOOGLE
};

// The bytes to write for a document: sealed with the key when there is one, plain JSON otherwise.
[[nodiscard]] std::expected<QByteArray, StorageError> EncodeStoredDocument(const QJsonObject& document, const std::optional<DataKey>& key, SealedFileKind kind, const QString& filePath);

// Reads plain and sealed files alike, so turning encryption on or off can continue after an interruption. A sealed
// file is bound to its kind and file name: renamed or swapped, it does not open.
[[nodiscard]] std::expected<QJsonObject, StorageError> ReadStoredDocument(const QFileInfo& fileInfo, qint64 maxFileSize, const std::optional<DataKey>& key, SealedFileKind kind);

} // namespace SteamForge
