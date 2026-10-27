#include "cloud/cloud_record.h"

#include "common/encoding.h"
#include "storage/record_serialization.h"

#include <openssl/crypto.h>

namespace SteamForge {

namespace {
	constexpr QLatin1StringView CLOUD_RECORD_FORMAT{"steamforge-cloud-record"};
	constexpr int CLOUD_RECORD_FORMAT_VERSION = 1;
	constexpr QLatin1StringView FORMAT_FIELD{"format"};
	constexpr QLatin1StringView VERSION_FIELD{"version"};
	constexpr QLatin1StringView NONCE_FIELD{"nonce"};
	constexpr QLatin1StringView CIPHERTEXT_FIELD{"ciphertext"};
	constexpr QLatin1StringView ASSOCIATED_DATA_PATTERN{"steamforge-cloud-record/1/%1/%2/%3"};
	// The Session object of a .maFile, with Steam sign-in tokens.
	constexpr QLatin1StringView SESSION_FIELD{"Session"};

	QByteArray AssociatedData(const CloudRecordPlace& place) {
		return QString(ASSOCIATED_DATA_PATTERN).arg(place.vaultId, place.recordId, QString::number(place.revision)).toUtf8();
	}

	void Wipe(QByteArray& bytes) {
		OPENSSL_cleanse(bytes.data(), static_cast<std::size_t>(bytes.size()));
	}
} // namespace

AuthenticatorRecord WithoutSteamSignIn(AuthenticatorRecord record) {
	record.additionalFields.remove(SESSION_FIELD);
	return record;
}

AuthenticatorRecord WithLocalSteamSignIn(AuthenticatorRecord cloudRecord, const AuthenticatorRecord& localRecord) {
	if (localRecord.additionalFields.contains(SESSION_FIELD) && localRecord.account.steamId == cloudRecord.account.steamId) {
		cloudRecord.additionalFields.insert(SESSION_FIELD, localRecord.additionalFields.value(SESSION_FIELD));
	}
	return cloudRecord;
}

QString CloudRecordFingerprint(const AuthenticatorRecord& record) {
	QByteArray serialized = QJsonDocument(SerializeRecord(WithoutSteamSignIn(record))).toJson(QJsonDocument::Compact);
	const QByteArray digest = QCryptographicHash::hash(serialized, QCryptographicHash::Sha256);
	Wipe(serialized);
	return QString::fromLatin1(digest.toBase64());
}

std::optional<QByteArray> SealCloudRecord(const DataKey& cloudKey, const CloudRecordPlace& place, const AuthenticatorRecord& record) {
	QByteArray plaintext = QJsonDocument(SerializeRecord(WithoutSteamSignIn(record))).toJson(QJsonDocument::Compact);
	const std::optional<SealedData> sealed = Seal(cloudKey, plaintext, AssociatedData(place));
	Wipe(plaintext);
	if (!sealed.has_value()) {
		return std::nullopt;
	}
	const QJsonObject envelope{
		{FORMAT_FIELD, CLOUD_RECORD_FORMAT},
		{VERSION_FIELD, CLOUD_RECORD_FORMAT_VERSION},
		{NONCE_FIELD, QString::fromLatin1(sealed->nonce.toBase64())},
		{CIPHERTEXT_FIELD, QString::fromLatin1(sealed->ciphertext.toBase64())},
	};
	return QJsonDocument(envelope).toJson(QJsonDocument::Compact);
}

std::expected<AuthenticatorRecord, CloudRecordError> OpenCloudRecord(const DataKey& cloudKey, const CloudRecordPlace& place, const QByteArray& fileContent) {
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(fileContent, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject() || document.object().value(FORMAT_FIELD).toString() != CLOUD_RECORD_FORMAT) {
		return std::unexpected(CloudRecordError::NOT_A_RECORD);
	}
	const QJsonObject envelope = document.object();
	const int version = envelope.value(VERSION_FIELD).toInt();
	if (version > CLOUD_RECORD_FORMAT_VERSION) {
		return std::unexpected(CloudRecordError::NEWER_VERSION);
	}
	const std::optional<QByteArray> nonce = DecodeBase64(envelope.value(NONCE_FIELD).toString());
	const std::optional<QByteArray> ciphertext = DecodeBase64(envelope.value(CIPHERTEXT_FIELD).toString());
	if (version != CLOUD_RECORD_FORMAT_VERSION || !nonce.has_value() || !ciphertext.has_value()) {
		return std::unexpected(CloudRecordError::DAMAGED);
	}
	std::optional<QByteArray> plaintext = Unseal(cloudKey, SealedData{.nonce = *nonce, .ciphertext = *ciphertext}, AssociatedData(place));
	if (!plaintext.has_value()) {
		return std::unexpected(CloudRecordError::UNSEALING_FAILED);
	}
	const QJsonDocument recordDocument = QJsonDocument::fromJson(*plaintext);
	Wipe(*plaintext);
	std::expected<AuthenticatorRecord, StorageError> record = DeserializeRecord(recordDocument.object());
	if (!record.has_value()) {
		return std::unexpected(CloudRecordError::DAMAGED);
	}
	return std::move(*record);
}

} // namespace SteamForge
