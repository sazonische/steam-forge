#pragma once

#include "crypto/data_sealing.h"
#include "storage/storage_error.h"

namespace SteamForge {

enum class SealedFileKind {
	ACCOUNT,
	SESSION,
	GOOGLE
};

// The bytes to write for a document: sealed with the key when there is one, plain JSON otherwise. A document larger
// than maxDocumentSize is refused, so whatever is written opens again with the same limit in either form.
[[nodiscard]] std::expected<QByteArray, StorageError> EncodeStoredDocument(const QJsonObject& document, qint64 maxDocumentSize, const std::optional<DataKey>& key, SealedFileKind kind, const QString& filePath);

// Reads plain and sealed files alike, so turning encryption on or off can continue after an interruption. A sealed
// file is bound to its kind and file name: renamed or swapped, it does not open. maxDocumentSize limits the JSON of
// the document, so a sealed file may be larger by its encoding.
[[nodiscard]] std::expected<QJsonObject, StorageError> ReadStoredDocument(const QFileInfo& fileInfo, qint64 maxDocumentSize, const std::optional<DataKey>& key, SealedFileKind kind);

// The size of the JSON that EncodeStoredDocument writes or seals for the document.
[[nodiscard]] qint64 StoredDocumentSize(const QJsonObject& document);

} // namespace SteamForge
