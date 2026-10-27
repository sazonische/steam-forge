#include "storage/sealed_file.h"

#include "storage/private_file.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView SEALED_FORMAT_NAME{"steamforge-sealed"};
	constexpr int SEALED_FORMAT_VERSION = 1;
	constexpr QLatin1StringView CIPHER_NAME{"chacha20-poly1305"};
	constexpr QLatin1StringView FORMAT_KEY{"format"};
	constexpr QLatin1StringView VERSION_KEY{"formatVersion"};
	constexpr QLatin1StringView CIPHER_KEY{"cipher"};
	constexpr QLatin1StringView NONCE_KEY{"nonce"};
	constexpr QLatin1StringView DATA_KEY{"data"};
	constexpr QLatin1StringView ASSOCIATED_DATA_PATTERN{"steamforge-sealed/%1/%2/%3"};
	constexpr QLatin1StringView ACCOUNT_KIND_NAME{"account"};
	constexpr QLatin1StringView SESSION_KIND_NAME{"session"};
	constexpr QLatin1StringView GOOGLE_KIND_NAME{"google"};
	// The cipher tag, the nonce, the field names and the indentation of the envelope fit in it with room to spare.
	constexpr qint64 ENVELOPE_ALLOWANCE = qint64{4} << 10;

	// Base64 writes 4 characters for every 3 bytes of the ciphertext.
	qint64 SealedFileSizeLimit(qint64 maxDocumentSize) {
		return ((maxDocumentSize + 2) / 3 * 4) + ENVELOPE_ALLOWANCE;
	}

	QLatin1StringView KindName(SealedFileKind kind) {
		switch (kind) {
			case SealedFileKind::ACCOUNT:
				return ACCOUNT_KIND_NAME;
			case SealedFileKind::SESSION:
				return SESSION_KIND_NAME;
			case SealedFileKind::GOOGLE:
				return GOOGLE_KIND_NAME;
		}
		return {};
	}

	// The file name is the record ID, so a sealed file only opens under its own name.
	QByteArray AssociatedData(SealedFileKind kind, const QString& filePath) {
		return QString(ASSOCIATED_DATA_PATTERN).arg(QString::number(SEALED_FORMAT_VERSION), KindName(kind), QFileInfo(filePath).completeBaseName()).toUtf8();
	}

	std::unexpected<StorageError> Failure(StorageErrorKind kind, const QString& filePath, const QString& detail = {}) {
		return std::unexpected(StorageError{
			.kind = kind,
			.filePath = filePath,
			.detail = detail,
		});
	}
} // namespace

std::expected<QByteArray, StorageError> EncodeStoredDocument(const QJsonObject& document, qint64 maxDocumentSize, const std::optional<DataKey>& key, SealedFileKind kind, const QString& filePath) {
	QByteArray json = QJsonDocument(document).toJson(QJsonDocument::Indented);
	if (json.size() > maxDocumentSize) {
		return Failure(StorageErrorKind::FILE_TOO_LARGE, filePath);
	}
	if (!key.has_value()) {
		return json;
	}
	const std::optional<SealedData> sealed = Seal(*key, json, AssociatedData(kind, filePath));
	if (!sealed.has_value()) {
		return Failure(StorageErrorKind::WRITE_FAILED, filePath, QStringLiteral("encryption"));
	}
	const QJsonObject envelope{
		{FORMAT_KEY, SEALED_FORMAT_NAME},
		{VERSION_KEY, SEALED_FORMAT_VERSION},
		{CIPHER_KEY, CIPHER_NAME},
		{NONCE_KEY, QString::fromLatin1(sealed->nonce.toBase64())},
		{DATA_KEY, QString::fromLatin1(sealed->ciphertext.toBase64())},
	};
	return QJsonDocument(envelope).toJson(QJsonDocument::Indented);
}

std::expected<QJsonObject, StorageError> ReadStoredDocument(const QFileInfo& fileInfo, qint64 maxDocumentSize, const std::optional<DataKey>& key, SealedFileKind kind) {
	std::expected<QJsonObject, StorageError> document = ReadJsonObjectFile(fileInfo, SealedFileSizeLimit(maxDocumentSize));
	if (!document.has_value()) {
		return document;
	}
	const QString filePath = fileInfo.filePath();
	// A plain file is the document itself.
	if (document->value(FORMAT_KEY).toString() != SEALED_FORMAT_NAME) {
		if (fileInfo.size() > maxDocumentSize) {
			return Failure(StorageErrorKind::FILE_TOO_LARGE, filePath);
		}
		return document;
	}
	if (document->value(VERSION_KEY).toInteger() > SEALED_FORMAT_VERSION) {
		return Failure(StorageErrorKind::NEWER_FORMAT_VERSION, filePath, VERSION_KEY);
	}
	if (!key.has_value()) {
		return Failure(StorageErrorKind::KEY_UNAVAILABLE, filePath);
	}
	if (document->value(CIPHER_KEY).toString() != CIPHER_NAME) {
		return Failure(StorageErrorKind::UNKNOWN_FORMAT, filePath, CIPHER_KEY);
	}
	const SealedData sealed{
		.nonce = QByteArray::fromBase64(document->value(NONCE_KEY).toString().toLatin1()),
		.ciphertext = QByteArray::fromBase64(document->value(DATA_KEY).toString().toLatin1()),
	};
	const std::optional<QByteArray> json = Unseal(*key, sealed, AssociatedData(kind, filePath));
	if (!json.has_value()) {
		return Failure(StorageErrorKind::UNSEALING_FAILED, filePath);
	}
	if (json->size() > maxDocumentSize) {
		return Failure(StorageErrorKind::FILE_TOO_LARGE, filePath);
	}
	QJsonParseError parseError;
	const QJsonDocument inner = QJsonDocument::fromJson(*json, &parseError);
	if (parseError.error != QJsonParseError::NoError || !inner.isObject()) {
		return Failure(StorageErrorKind::INVALID_JSON, filePath, parseError.errorString());
	}
	return inner.object();
}

qint64 StoredDocumentSize(const QJsonObject& document) {
	return QJsonDocument(document).toJson(QJsonDocument::Indented).size();
}

} // namespace SteamForge
