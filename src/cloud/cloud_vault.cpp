#include "cloud/cloud_vault.h"

#include "common/encoding.h"
#include "crypto/key_derivation.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace SteamForge {

namespace {
	constexpr QLatin1StringView VAULT_FORMAT{"steamforge-cloud-vault"};
	constexpr int VAULT_FORMAT_VERSION = 1;
	constexpr QLatin1StringView FORMAT_FIELD{"format"};
	constexpr QLatin1StringView VERSION_FIELD{"version"};
	constexpr QLatin1StringView VAULT_ID_FIELD{"vaultId"};
	constexpr QLatin1StringView CREATED_AT_FIELD{"createdAt"};
	constexpr QLatin1StringView KEY_SLOTS_FIELD{"keySlots"};
	constexpr QLatin1StringView SLOT_KIND_FIELD{"kind"};
	constexpr QLatin1StringView SALT_FIELD{"salt"};
	constexpr QLatin1StringView NONCE_FIELD{"nonce"};
	constexpr QLatin1StringView WRAPPED_KEY_FIELD{"wrappedKey"};
	constexpr QLatin1StringView RECOVERY_KEY_SLOT_KIND{"recovery-key"};
	constexpr QByteArrayView RECOVERY_KEY_DERIVATION_INFO{"steamforge-cloud-vault/1/recovery-key"};
	// Binds the wrapped key to its vault, so a slot copied into another header does not open.
	constexpr QLatin1StringView RECOVERY_SLOT_ASSOCIATED_DATA_PATTERN{"steamforge-cloud-vault/1/%1/recovery-key"};
	constexpr qsizetype SALT_LENGTH = 16;
	constexpr qsizetype NONCE_LENGTH = 12;
	constexpr qsizetype TAG_LENGTH = 16;
	constexpr qsizetype KEY_GROUP_LENGTH = 4;
	constexpr qsizetype RECOVERY_KEY_TEXT_LENGTH = 32;
	constexpr QChar KEY_GROUP_SEPARATOR{u'-'};

	void Wipe(QByteArray& bytes) {
		OPENSSL_cleanse(bytes.data(), static_cast<std::size_t>(bytes.size()));
	}

	// Characters a person may type for the letters of the key; Base32 itself has no 0, 1 or 8.
	QChar LetterForLookAlike(QChar character) {
		if (character == u'0') {
			return u'O';
		}
		if (character == u'1') {
			return u'I';
		}
		if (character == u'8') {
			return u'B';
		}
		return character;
	}

	QByteArray AssociatedData(const QString& vaultId) {
		return QString(RECOVERY_SLOT_ASSOCIATED_DATA_PATTERN).arg(vaultId).toUtf8();
	}

	std::optional<DataKey> WrappingKey(const RecoveryKey& recoveryKey, QByteArrayView salt) {
		std::optional<QByteArray> derived = DeriveKeyBytes(recoveryKey.Bytes(), salt, RECOVERY_KEY_DERIVATION_INFO, DATA_KEY_LENGTH);
		if (!derived.has_value()) {
			return std::nullopt;
		}
		std::optional<DataKey> key = DataKey::FromBytes(*derived);
		Wipe(*derived);
		return key;
	}

	QString EncodeField(const QByteArray& bytes) {
		return QString::fromLatin1(bytes.toBase64());
	}

	std::optional<QByteArray> DecodeField(const QJsonObject& object, QLatin1StringView field, qsizetype expectedLength) {
		std::optional<QByteArray> decoded = DecodeBase64(object.value(field).toString());
		if (!decoded.has_value() || decoded->size() != expectedLength) {
			return std::nullopt;
		}
		return decoded;
	}
} // namespace

std::optional<RecoveryKey> RecoveryKey::Generate() {
	RecoveryKey key;
	if (RAND_bytes(key.bytes.data(), static_cast<int>(key.bytes.size())) != 1) {
		return std::nullopt;
	}
	return key;
}

std::optional<RecoveryKey> RecoveryKey::Parse(QStringView typedKey) {
	QString normalized;
	normalized.reserve(RECOVERY_KEY_TEXT_LENGTH);
	for (const QChar character : typedKey) {
		if (character == KEY_GROUP_SEPARATOR || character.isSpace()) {
			continue;
		}
		normalized.append(LetterForLookAlike(character.toUpper()));
	}
	if (normalized.size() != RECOVERY_KEY_TEXT_LENGTH) {
		return std::nullopt;
	}
	std::optional<QByteArray> decoded = DecodeBase32(normalized);
	if (!decoded.has_value()) {
		return std::nullopt;
	}
	if (decoded->size() != RECOVERY_KEY_LENGTH) {
		Wipe(*decoded);
		return std::nullopt;
	}
	RecoveryKey key;
	std::ranges::copy(*decoded, key.bytes.begin());
	Wipe(*decoded);
	return key;
}

RecoveryKey::~RecoveryKey() {
	OPENSSL_cleanse(bytes.data(), bytes.size());
}

QString RecoveryKey::Text() const {
	QByteArray keyBytes = Bytes().toByteArray();
	const QString encoded = EncodeBase32(keyBytes);
	Wipe(keyBytes);
	QString grouped;
	grouped.reserve(encoded.size() + (encoded.size() / KEY_GROUP_LENGTH));
	for (qsizetype i = 0; i < encoded.size(); i += KEY_GROUP_LENGTH) {
		if (!grouped.isEmpty()) {
			grouped.append(KEY_GROUP_SEPARATOR);
		}
		grouped.append(encoded.mid(i, KEY_GROUP_LENGTH));
	}
	return grouped;
}

QByteArrayView RecoveryKey::Bytes() const {
	return {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())};
}

std::optional<CloudVaultHeader> CreateVaultHeader(const QString& vaultId, const QDateTime& createdAt, const DataKey& cloudKey, const RecoveryKey& recoveryKey) {
	QByteArray salt(SALT_LENGTH, '\0');
	if (RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()), static_cast<int>(salt.size())) != 1) {
		return std::nullopt;
	}
	const std::optional<DataKey> wrappingKey = WrappingKey(recoveryKey, salt);
	if (!wrappingKey.has_value()) {
		return std::nullopt;
	}
	std::optional<SealedData> wrapped = Seal(*wrappingKey, cloudKey.Bytes(), AssociatedData(vaultId));
	if (!wrapped.has_value()) {
		return std::nullopt;
	}
	return CloudVaultHeader{
		.vaultId = vaultId,
		.createdAt = createdAt.toUTC(),
		.recoverySlot = RecoveryKeySlot{.salt = std::move(salt), .wrappedKey = std::move(*wrapped)},
	};
}

QByteArray SerializeVaultHeader(const CloudVaultHeader& header) {
	const QJsonObject recoverySlot{
		{SLOT_KIND_FIELD, RECOVERY_KEY_SLOT_KIND},
		{SALT_FIELD, EncodeField(header.recoverySlot.salt)},
		{NONCE_FIELD, EncodeField(header.recoverySlot.wrappedKey.nonce)},
		{WRAPPED_KEY_FIELD, EncodeField(header.recoverySlot.wrappedKey.ciphertext)},
	};
	const QJsonObject document{
		{FORMAT_FIELD, VAULT_FORMAT},
		{VERSION_FIELD, VAULT_FORMAT_VERSION},
		{VAULT_ID_FIELD, header.vaultId},
		{CREATED_AT_FIELD, header.createdAt.toUTC().toString(Qt::ISODateWithMs)},
		{KEY_SLOTS_FIELD, QJsonArray{recoverySlot}},
	};
	return QJsonDocument(document).toJson(QJsonDocument::Compact);
}

std::expected<CloudVaultHeader, VaultHeaderError> ParseVaultHeader(const QByteArray& headerJson) {
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(headerJson, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject() || document.object().value(FORMAT_FIELD).toString() != VAULT_FORMAT) {
		return std::unexpected(VaultHeaderError::NOT_A_VAULT);
	}
	const QJsonObject object = document.object();
	const int version = object.value(VERSION_FIELD).toInt();
	if (version > VAULT_FORMAT_VERSION) {
		return std::unexpected(VaultHeaderError::NEWER_VERSION);
	}
	const QString vaultId = object.value(VAULT_ID_FIELD).toString();
	const QDateTime createdAt = QDateTime::fromString(object.value(CREATED_AT_FIELD).toString(), Qt::ISODateWithMs);
	if (version != VAULT_FORMAT_VERSION || QUuid::fromString(vaultId).isNull() || !createdAt.isValid()) {
		return std::unexpected(VaultHeaderError::DAMAGED);
	}
	const QJsonArray slots = object.value(KEY_SLOTS_FIELD).toArray();
	const auto recoverySlot = std::ranges::find_if(slots, [](const QJsonValue& slot) {
		return slot.toObject().value(SLOT_KIND_FIELD).toString() == RECOVERY_KEY_SLOT_KIND;
	});
	if (recoverySlot == slots.end()) {
		return std::unexpected(VaultHeaderError::DAMAGED);
	}
	const QJsonObject slotObject = (*recoverySlot).toObject();
	std::optional<QByteArray> salt = DecodeField(slotObject, SALT_FIELD, SALT_LENGTH);
	std::optional<QByteArray> nonce = DecodeField(slotObject, NONCE_FIELD, NONCE_LENGTH);
	std::optional<QByteArray> wrappedKey = DecodeField(slotObject, WRAPPED_KEY_FIELD, DATA_KEY_LENGTH + TAG_LENGTH);
	if (!salt.has_value() || !nonce.has_value() || !wrappedKey.has_value()) {
		return std::unexpected(VaultHeaderError::DAMAGED);
	}
	return CloudVaultHeader{
		.vaultId = vaultId,
		.createdAt = createdAt.toUTC(),
		.recoverySlot = RecoveryKeySlot{.salt = std::move(*salt), .wrappedKey = SealedData{.nonce = std::move(*nonce), .ciphertext = std::move(*wrappedKey)}},
	};
}

std::optional<DataKey> OpenWithRecoveryKey(const CloudVaultHeader& header, const RecoveryKey& recoveryKey) {
	const std::optional<DataKey> wrappingKey = WrappingKey(recoveryKey, header.recoverySlot.salt);
	if (!wrappingKey.has_value()) {
		return std::nullopt;
	}
	std::optional<QByteArray> unwrapped = Unseal(*wrappingKey, header.recoverySlot.wrappedKey, AssociatedData(header.vaultId));
	if (!unwrapped.has_value()) {
		return std::nullopt;
	}
	std::optional<DataKey> cloudKey = DataKey::FromBytes(*unwrapped);
	Wipe(*unwrapped);
	return cloudKey;
}

} // namespace SteamForge
