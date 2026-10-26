#include "storage/record_serialization.h"

#include "common/encoding.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView FORMAT_NAME{"steamforge-authenticator"};

	constexpr QLatin1StringView FORMAT_KEY{"format"};
	constexpr QLatin1StringView FORMAT_VERSION_KEY{"formatVersion"};
	constexpr QLatin1StringView ID_KEY{"id"};
	constexpr QLatin1StringView LABEL_KEY{"label"};
	constexpr QLatin1StringView CREATED_AT_KEY{"createdAt"};
	constexpr QLatin1StringView ACCOUNT_KEY{"account"};
	constexpr QLatin1StringView STEAM_ID_KEY{"steamId"};
	constexpr QLatin1StringView ACCOUNT_NAME_KEY{"accountName"};
	constexpr QLatin1StringView SOURCE_KEY{"source"};
	constexpr QLatin1StringView ORIGIN_KEY{"origin"};
	constexpr QLatin1StringView FILE_NAME_KEY{"fileName"};
	constexpr QLatin1StringView IMPORTED_AT_KEY{"importedAt"};
	constexpr QLatin1StringView SECRETS_KEY{"secrets"};
	constexpr QLatin1StringView SHARED_SECRET_KEY{"sharedSecret"};
	constexpr QLatin1StringView IDENTITY_SECRET_KEY{"identitySecret"};
	constexpr QLatin1StringView REVOCATION_CODE_KEY{"revocationCode"};
	constexpr QLatin1StringView DEVICE_ID_KEY{"deviceId"};
	constexpr QLatin1StringView SERIAL_NUMBER_KEY{"serialNumber"};
	constexpr QLatin1StringView TOKEN_GID_KEY{"tokenGid"};
	constexpr QLatin1StringView ADDITIONAL_FIELDS_KEY{"additionalFields"};
	constexpr QLatin1StringView PENDING_ACTIVATION_KEY{"pendingActivation"};
	constexpr QLatin1StringView CONFIRM_TYPE_KEY{"confirmType"};
	constexpr QLatin1StringView PHONE_NUMBER_HINT_KEY{"phoneNumberHint"};

	constexpr QLatin1StringView MA_FILE_ORIGIN{"maFile"};
	constexpr QLatin1StringView SDA_ENCRYPTED_MA_FILE_ORIGIN{"sdaEncryptedMaFile"};
	constexpr QLatin1StringView NODE_STEAMCOMMUNITY_FILE_ORIGIN{"nodeSteamcommunityFile"};
	constexpr QLatin1StringView OTPAUTH_URI_ORIGIN{"otpauthUri"};
	constexpr QLatin1StringView STEAM_SIGN_IN_ORIGIN{"steamSignIn"};
	constexpr QLatin1StringView ADDED_IN_STEAMFORGE_ORIGIN{"addedInSteamForge"};
	constexpr QLatin1StringView MOVED_TO_STEAMFORGE_ORIGIN{"movedToSteamForge"};

	constexpr int FIRST_FORMAT_VERSION = 1;

	QLatin1StringView OriginName(RecordOrigin origin) {
		switch (origin) {
			case RecordOrigin::MA_FILE:
				return MA_FILE_ORIGIN;
			case RecordOrigin::SDA_ENCRYPTED_MA_FILE:
				return SDA_ENCRYPTED_MA_FILE_ORIGIN;
			case RecordOrigin::NODE_STEAMCOMMUNITY_FILE:
				return NODE_STEAMCOMMUNITY_FILE_ORIGIN;
			case RecordOrigin::OTPAUTH_URI:
				return OTPAUTH_URI_ORIGIN;
			case RecordOrigin::STEAM_SIGN_IN:
				return STEAM_SIGN_IN_ORIGIN;
			case RecordOrigin::ADDED_IN_STEAMFORGE:
				return ADDED_IN_STEAMFORGE_ORIGIN;
			case RecordOrigin::MOVED_TO_STEAMFORGE:
				return MOVED_TO_STEAMFORGE_ORIGIN;
		}
		return MA_FILE_ORIGIN;
	}

	std::optional<RecordOrigin> ParseOrigin(const QString& originName) {
		if (originName == MA_FILE_ORIGIN) {
			return RecordOrigin::MA_FILE;
		}
		if (originName == SDA_ENCRYPTED_MA_FILE_ORIGIN) {
			return RecordOrigin::SDA_ENCRYPTED_MA_FILE;
		}
		if (originName == NODE_STEAMCOMMUNITY_FILE_ORIGIN) {
			return RecordOrigin::NODE_STEAMCOMMUNITY_FILE;
		}
		if (originName == OTPAUTH_URI_ORIGIN) {
			return RecordOrigin::OTPAUTH_URI;
		}
		if (originName == STEAM_SIGN_IN_ORIGIN) {
			return RecordOrigin::STEAM_SIGN_IN;
		}
		if (originName == ADDED_IN_STEAMFORGE_ORIGIN) {
			return RecordOrigin::ADDED_IN_STEAMFORGE;
		}
		if (originName == MOVED_TO_STEAMFORGE_ORIGIN) {
			return RecordOrigin::MOVED_TO_STEAMFORGE;
		}
		return std::nullopt;
	}

	QString FormatTimestamp(const QDateTime& timestamp) {
		return timestamp.toUTC().toString(Qt::ISODateWithMs);
	}

	std::optional<QDateTime> ParseTimestamp(const QJsonValue& value) {
		QDateTime timestamp = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
		if (!timestamp.isValid()) {
			return std::nullopt;
		}
		return timestamp;
	}

	void InsertIfNotEmpty(QJsonObject& object, QLatin1StringView key, const QString& value) {
		if (!value.isEmpty()) {
			object.insert(key, value);
		}
	}

	void InsertBase64IfNotEmpty(QJsonObject& object, QLatin1StringView key, const QByteArray& value) {
		if (!value.isEmpty()) {
			object.insert(key, QString::fromLatin1(value.toBase64()));
		}
	}

	std::unexpected<StorageError> InvalidField(QLatin1StringView fieldName) {
		return std::unexpected(StorageError{
			.kind = StorageErrorKind::INVALID_CONTENT,
			.filePath = {},
			.detail = fieldName,
		});
	}

	std::optional<QString> ReadOptionalString(const QJsonObject& object, QLatin1StringView key) {
		const QJsonValue value = object.value(key);
		if (value.isUndefined()) {
			return QString();
		}
		if (!value.isString()) {
			return std::nullopt;
		}
		return value.toString();
	}

	std::optional<QByteArray> ReadOptionalBase64(const QJsonObject& object, QLatin1StringView key) {
		const std::optional<QString> encoded = ReadOptionalString(object, key);
		if (!encoded.has_value()) {
			return std::nullopt;
		}
		return DecodeBase64(*encoded);
	}

	std::expected<SteamAccountIdentity, StorageError> ReadAccount(const QJsonObject& accountObject) {
		SteamAccountIdentity account;
		const std::optional<QString> steamIdText = ReadOptionalString(accountObject, STEAM_ID_KEY);
		if (!steamIdText.has_value()) {
			return InvalidField(STEAM_ID_KEY);
		}
		if (!steamIdText->isEmpty()) {
			bool isConverted = false;
			const quint64 steamId = steamIdText->toULongLong(&isConverted);
			if (!isConverted || !IsIndividualSteamId(steamId)) {
				return InvalidField(STEAM_ID_KEY);
			}
			account.steamId = steamId;
		}
		const std::optional<QString> accountName = ReadOptionalString(accountObject, ACCOUNT_NAME_KEY);
		if (!accountName.has_value()) {
			return InvalidField(ACCOUNT_NAME_KEY);
		}
		account.accountName = *accountName;
		return account;
	}

	std::expected<RecordSource, StorageError> ReadSource(const QJsonObject& sourceObject) {
		const std::optional<RecordOrigin> origin = ParseOrigin(sourceObject.value(ORIGIN_KEY).toString());
		if (!origin.has_value()) {
			return InvalidField(ORIGIN_KEY);
		}
		const std::optional<QString> fileName = ReadOptionalString(sourceObject, FILE_NAME_KEY);
		if (!fileName.has_value()) {
			return InvalidField(FILE_NAME_KEY);
		}
		const std::optional<QDateTime> importedAt = ParseTimestamp(sourceObject.value(IMPORTED_AT_KEY));
		if (!importedAt.has_value()) {
			return InvalidField(IMPORTED_AT_KEY);
		}
		return RecordSource{
			.origin = *origin,
			.fileName = *fileName,
			.importedAt = *importedAt,
		};
	}

	std::expected<SecretBundle, StorageError> ReadSecrets(const QJsonObject& secretsObject) {
		SecretBundle secrets;
		const std::optional<QByteArray> sharedSecret = ReadOptionalBase64(secretsObject, SHARED_SECRET_KEY);
		if (!sharedSecret.has_value()) {
			return InvalidField(SHARED_SECRET_KEY);
		}
		const std::optional<QByteArray> identitySecret = ReadOptionalBase64(secretsObject, IDENTITY_SECRET_KEY);
		if (!identitySecret.has_value()) {
			return InvalidField(IDENTITY_SECRET_KEY);
		}
		secrets.sharedSecret = *sharedSecret;
		secrets.identitySecret = *identitySecret;

		const std::array<std::pair<QLatin1StringView, QString*>, 4> textFields{{
			{REVOCATION_CODE_KEY, &secrets.revocationCode},
			{DEVICE_ID_KEY, &secrets.deviceId},
			{SERIAL_NUMBER_KEY, &secrets.serialNumber},
			{TOKEN_GID_KEY, &secrets.tokenGid},
		}};
		for (const auto& [key, target] : textFields) {
			const std::optional<QString> value = ReadOptionalString(secretsObject, key);
			if (!value.has_value()) {
				return InvalidField(key);
			}
			*target = *value;
		}
		return secrets;
	}

	std::expected<std::optional<PendingActivation>, StorageError> ReadPendingActivation(const QJsonValue& value) {
		if (value.isUndefined()) {
			return std::nullopt;
		}
		if (!value.isObject()) {
			return InvalidField(PENDING_ACTIVATION_KEY);
		}
		const QJsonObject activationObject = value.toObject();
		if (!activationObject.value(CONFIRM_TYPE_KEY).isDouble()) {
			return InvalidField(CONFIRM_TYPE_KEY);
		}
		const std::optional<QString> phoneNumberHint = ReadOptionalString(activationObject, PHONE_NUMBER_HINT_KEY);
		if (!phoneNumberHint.has_value()) {
			return InvalidField(PHONE_NUMBER_HINT_KEY);
		}
		return PendingActivation{
			.confirmType = activationObject.value(CONFIRM_TYPE_KEY).toInt(),
			.phoneNumberHint = *phoneNumberHint,
		};
	}
} // namespace

QJsonObject SerializeRecord(const AuthenticatorRecord& record) {
	QJsonObject account;
	if (record.account.steamId.has_value()) {
		// JSON readers often parse numbers as double, which cannot hold a SteamID64 exactly.
		account.insert(STEAM_ID_KEY, QString::number(*record.account.steamId));
	}
	InsertIfNotEmpty(account, ACCOUNT_NAME_KEY, record.account.accountName);

	QJsonObject source;
	source.insert(ORIGIN_KEY, OriginName(record.source.origin));
	InsertIfNotEmpty(source, FILE_NAME_KEY, record.source.fileName);
	source.insert(IMPORTED_AT_KEY, FormatTimestamp(record.source.importedAt));

	QJsonObject secrets;
	InsertBase64IfNotEmpty(secrets, SHARED_SECRET_KEY, record.secrets.sharedSecret);
	InsertBase64IfNotEmpty(secrets, IDENTITY_SECRET_KEY, record.secrets.identitySecret);
	InsertIfNotEmpty(secrets, REVOCATION_CODE_KEY, record.secrets.revocationCode);
	InsertIfNotEmpty(secrets, DEVICE_ID_KEY, record.secrets.deviceId);
	InsertIfNotEmpty(secrets, SERIAL_NUMBER_KEY, record.secrets.serialNumber);
	InsertIfNotEmpty(secrets, TOKEN_GID_KEY, record.secrets.tokenGid);

	QJsonObject document;
	document.insert(FORMAT_KEY, FORMAT_NAME);
	document.insert(FORMAT_VERSION_KEY, RECORD_FORMAT_VERSION);
	document.insert(ID_KEY, record.id.toString(QUuid::WithoutBraces));
	InsertIfNotEmpty(document, LABEL_KEY, record.label);
	document.insert(CREATED_AT_KEY, FormatTimestamp(record.createdAt));
	document.insert(ACCOUNT_KEY, account);
	document.insert(SOURCE_KEY, source);
	document.insert(SECRETS_KEY, secrets);
	if (record.pendingActivation.has_value()) {
		QJsonObject pendingActivation;
		pendingActivation.insert(CONFIRM_TYPE_KEY, record.pendingActivation->confirmType);
		InsertIfNotEmpty(pendingActivation, PHONE_NUMBER_HINT_KEY, record.pendingActivation->phoneNumberHint);
		document.insert(PENDING_ACTIVATION_KEY, pendingActivation);
	}
	if (!record.additionalFields.isEmpty()) {
		document.insert(ADDITIONAL_FIELDS_KEY, record.additionalFields);
	}
	return document;
}

std::expected<AuthenticatorRecord, StorageError> DeserializeRecord(const QJsonObject& document) {
	if (document.value(FORMAT_KEY).toString() != FORMAT_NAME) {
		return std::unexpected(StorageError{
			.kind = StorageErrorKind::UNKNOWN_FORMAT,
			.filePath = {},
			.detail = {},
		});
	}
	const QJsonValue formatVersion = document.value(FORMAT_VERSION_KEY);
	if (!formatVersion.isDouble() || formatVersion.toInteger() < FIRST_FORMAT_VERSION) {
		return InvalidField(FORMAT_VERSION_KEY);
	}
	if (formatVersion.toInteger() > RECORD_FORMAT_VERSION) {
		return std::unexpected(StorageError{
			.kind = StorageErrorKind::NEWER_FORMAT_VERSION,
			.filePath = {},
			.detail = QString::number(formatVersion.toInteger()),
		});
	}

	AuthenticatorRecord record;
	record.id = QUuid::fromString(document.value(ID_KEY).toString());
	if (record.id.isNull()) {
		return InvalidField(ID_KEY);
	}
	const std::optional<QString> label = ReadOptionalString(document, LABEL_KEY);
	if (!label.has_value()) {
		return InvalidField(LABEL_KEY);
	}
	record.label = *label;
	const std::optional<QDateTime> createdAt = ParseTimestamp(document.value(CREATED_AT_KEY));
	if (!createdAt.has_value()) {
		return InvalidField(CREATED_AT_KEY);
	}
	record.createdAt = *createdAt;

	const std::expected<SteamAccountIdentity, StorageError> account = ReadAccount(document.value(ACCOUNT_KEY).toObject());
	if (!account.has_value()) {
		return std::unexpected(account.error());
	}
	record.account = *account;

	const std::expected<RecordSource, StorageError> source = ReadSource(document.value(SOURCE_KEY).toObject());
	if (!source.has_value()) {
		return std::unexpected(source.error());
	}
	record.source = *source;

	const std::expected<SecretBundle, StorageError> secrets = ReadSecrets(document.value(SECRETS_KEY).toObject());
	if (!secrets.has_value()) {
		return std::unexpected(secrets.error());
	}
	record.secrets = *secrets;

	const std::expected<std::optional<PendingActivation>, StorageError> pendingActivation = ReadPendingActivation(document.value(PENDING_ACTIVATION_KEY));
	if (!pendingActivation.has_value()) {
		return std::unexpected(pendingActivation.error());
	}
	record.pendingActivation = *pendingActivation;

	const QJsonValue additionalFields = document.value(ADDITIONAL_FIELDS_KEY);
	if (!additionalFields.isUndefined() && !additionalFields.isObject()) {
		return InvalidField(ADDITIONAL_FIELDS_KEY);
	}
	record.additionalFields = additionalFields.toObject();
	return record;
}

} // namespace SteamForge
