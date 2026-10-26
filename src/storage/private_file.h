#pragma once

#include "storage/storage_error.h"

namespace SteamForge {

// Files with unencrypted secrets: replaced atomically and, on Linux, accessible only to the owner.

// In a folder of SteamForge, which itself becomes accessible only to the owner.
[[nodiscard]] std::expected<void, StorageError> WritePrivateFile(const QString& filePath, const QByteArray& contents);
// In a folder the user chose: only the file is restricted, the folder stays as it is.
[[nodiscard]] std::expected<void, StorageError> WriteOwnerOnlyFile(const QString& filePath, const QByteArray& contents);

[[nodiscard]] std::expected<QJsonObject, StorageError> ReadJsonObjectFile(const QFileInfo& fileInfo, qint64 maxFileSize);

[[nodiscard]] std::expected<void, StorageError> RemoveFileIfExists(const QString& filePath);

} // namespace SteamForge
