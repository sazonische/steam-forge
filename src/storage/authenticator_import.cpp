#include "storage/authenticator_import.h"

#include "common/encoding.h"
#include "crypto/legacy_decryption.h"
#include "steam/device_id.h"
#include "storage/authenticator_store.h"

namespace SteamForge {

namespace {
	// Larger files are not authenticator exports and are not read into memory.
	constexpr qint64 MAX_IMPORT_FILE_SIZE = qint64{4} << 20;
	constexpr std::string_view UTF8_BYTE_ORDER_MARK{"\xEF\xBB\xBF"};
	constexpr char JSON_OBJECT_START = '{';

	// Steam Desktop Authenticator encryption (SDA FileEncryptor.cs).
	constexpr QLatin1StringView SDA_MANIFEST_FILE_NAME{"manifest.json"};
	constexpr int SDA_PBKDF2_ITERATIONS = 50000;
	constexpr int SDA_KEY_LENGTH = 32;
	constexpr qsizetype SDA_INITIALIZATION_VECTOR_LENGTH = 16;
	constexpr qsizetype SDA_CIPHER_BLOCK_LENGTH = 16;
	constexpr QLatin1StringView MANIFEST_ENTRIES_KEY{"entries"};
	constexpr QLatin1StringView MANIFEST_ENCRYPTED_KEY{"encrypted"};
	constexpr QLatin1StringView MANIFEST_FILE_NAME_KEY{"filename"};
	constexpr QLatin1StringView MANIFEST_SALT_KEY{"encryption_salt"};
	constexpr QLatin1StringView MANIFEST_INITIALIZATION_VECTOR_KEY{"encryption_iv"};

	// Field names of the Steam AddAuthenticator response, which .maFile and WinAuth data reuse.
	constexpr QLatin1StringView SHARED_SECRET_KEY{"shared_secret"};
	constexpr QLatin1StringView IDENTITY_SECRET_KEY{"identity_secret"};
	constexpr QLatin1StringView REVOCATION_CODE_KEY{"revocation_code"};
	constexpr QLatin1StringView DEVICE_ID_KEY{"device_id"};
	constexpr QLatin1StringView SERIAL_NUMBER_KEY{"serial_number"};
	constexpr QLatin1StringView TOKEN_GID_KEY{"token_gid"};
	constexpr QLatin1StringView ACCOUNT_NAME_KEY{"account_name"};
	constexpr QLatin1StringView STEAM_ID_KEY{"steamid"};
	constexpr QLatin1StringView SESSION_KEY{"Session"};
	constexpr QLatin1StringView SESSION_STEAM_ID_KEY{"SteamID"};

	// otpauth links (WinAuth export, Key URI format).
	constexpr QLatin1StringView OTPAUTH_SCHEME{"otpauth"};
	constexpr QLatin1StringView OTPAUTH_LINK_PREFIX{"otpauth://"};
	constexpr QLatin1StringView TOTP_LINK_TYPE{"totp"};
	// Some authenticator apps write Steam entries as otpauth://steam/.
	constexpr QLatin1StringView STEAM_LINK_TYPE{"steam"};
	constexpr QLatin1StringView STEAM_ISSUER{"Steam"};
	constexpr QLatin1StringView SECRET_PARAMETER{"secret"};
	constexpr QLatin1StringView ISSUER_PARAMETER{"issuer"};
	constexpr QLatin1StringView DEVICE_ID_PARAMETER{"deviceid"};
	constexpr QLatin1StringView DATA_PARAMETER{"data"};
	constexpr QLatin1StringView ENCODED_SPACE{"%20"};
	constexpr char16_t LABEL_ISSUER_SEPARATOR = u':';
	constexpr char16_t PATH_SEPARATOR = u'/';
	constexpr char16_t FORM_ENCODED_SPACE = u'+';

	// node-steamcommunity examples/enable_twofactor.js saves the AddAuthenticator reply under this name.
	constexpr QLatin1StringView NODE_STEAMCOMMUNITY_FILE_NAME_PATTERN{"^twofactor_(\\d{17})\\.json$"};

	std::unexpected<ImportError> Failure(ImportErrorKind kind, const QString& detail = {}) {
		return std::unexpected(ImportError{
			.kind = kind,
			.detail = detail,
		});
	}

	QByteArray WithoutByteOrderMark(const QByteArray& contents) {
		if (contents.startsWith(QByteArrayView(UTF8_BYTE_ORDER_MARK))) {
			return contents.sliced(static_cast<qsizetype>(UTF8_BYTE_ORDER_MARK.size()));
		}
		return contents;
	}

	std::expected<QByteArray, ImportError> ReadLimitedFile(const QString& filePath) {
		QFile file(filePath);
		if (file.size() > MAX_IMPORT_FILE_SIZE) {
			return Failure(ImportErrorKind::FILE_TOO_LARGE);
		}
		if (!file.open(QIODevice::ReadOnly)) {
			return Failure(ImportErrorKind::FILE_UNREADABLE, file.errorString());
		}
		return WithoutByteOrderMark(file.readAll()).trimmed();
	}

	AuthenticatorRecord NewRecord(RecordOrigin origin, const QString& fileName) {
		const QDateTime now = QDateTime::currentDateTimeUtc();
		AuthenticatorRecord record;
		record.id = QUuid::createUuid();
		record.createdAt = now;
		record.source = {
			.origin = origin,
			.fileName = fileName,
			.importedAt = now,
		};
		return record;
	}

	// Takes the value out of `fields` only when it is a string, so values of other types stay preserved.
	QString TakeString(QJsonObject& fields, QLatin1StringView key) {
		const QJsonValue value = fields.value(key);
		if (!value.isString()) {
			return {};
		}
		fields.remove(key);
		return value.toString();
	}

	std::expected<QByteArray, ImportError> TakeBase64(QJsonObject& fields, QLatin1StringView key) {
		const QJsonValue value = fields.value(key);
		if (value.isUndefined() || value.isNull()) {
			return QByteArray();
		}
		const std::optional<QByteArray> decoded = value.isString() ? DecodeBase64(value.toString()) : std::nullopt;
		if (!decoded.has_value()) {
			return Failure(ImportErrorKind::INVALID_CONTENT, key);
		}
		fields.remove(key);
		return *decoded;
	}

	std::optional<quint64> ReadSteamId(const QJsonValue& value) {
		std::optional<quint64> steamId;
		if (value.isString()) {
			bool isConverted = false;
			const quint64 number = value.toString().toULongLong(&isConverted);
			if (isConverted) {
				steamId = number;
			}
		} else if (value.isDouble()) {
			// Qt keeps JSON integers as qint64, so a SteamID64 survives without rounding through double.
			const qint64 number = value.toInteger(0);
			if (number > 0) {
				steamId = static_cast<quint64>(number);
			}
		}
		if (!steamId.has_value() || !IsIndividualSteamId(*steamId)) {
			return std::nullopt;
		}
		return steamId;
	}

	std::expected<AuthenticatorRecord, ImportError> RecordFromSteamFields(QJsonObject fields, RecordOrigin origin, const QString& fileName) {
		AuthenticatorRecord record = NewRecord(origin, fileName);
		const std::expected<QByteArray, ImportError> sharedSecret = TakeBase64(fields, SHARED_SECRET_KEY);
		if (!sharedSecret.has_value()) {
			return std::unexpected(sharedSecret.error());
		}
		const std::expected<QByteArray, ImportError> identitySecret = TakeBase64(fields, IDENTITY_SECRET_KEY);
		if (!identitySecret.has_value()) {
			return std::unexpected(identitySecret.error());
		}
		record.secrets.sharedSecret = *sharedSecret;
		record.secrets.identitySecret = *identitySecret;
		record.secrets.revocationCode = TakeString(fields, REVOCATION_CODE_KEY);
		record.secrets.deviceId = TakeString(fields, DEVICE_ID_KEY);
		record.secrets.serialNumber = TakeString(fields, SERIAL_NUMBER_KEY);
		record.secrets.tokenGid = TakeString(fields, TOKEN_GID_KEY);
		record.account.accountName = TakeString(fields, ACCOUNT_NAME_KEY);

		// WinAuth data has a top-level steamid; a .maFile keeps it in the preserved session object.
		record.account.steamId = ReadSteamId(fields.value(STEAM_ID_KEY));
		if (record.account.steamId.has_value()) {
			fields.remove(STEAM_ID_KEY);
		} else {
			record.account.steamId = ReadSteamId(fields.value(SESSION_KEY).toObject().value(SESSION_STEAM_ID_KEY));
		}
		// The session of the exporting program holds sign-in tokens SteamForge never uses, so only its SteamID is kept.
		if (const QJsonValue session = fields.value(SESSION_KEY); session.isObject()) {
			const QJsonValue sessionSteamId = session.toObject().value(SESSION_STEAM_ID_KEY);
			fields.insert(SESSION_KEY, sessionSteamId.isUndefined() ? QJsonObject() : QJsonObject{{SESSION_STEAM_ID_KEY, sessionSteamId}});
		}
		record.additionalFields = std::move(fields);
		return record;
	}

	// The file has no SteamID or device ID; the tool registered the device ID node-steam-totp derives from the SteamID.
	void ApplyNodeSteamcommunityFileConventions(AuthenticatorRecord& record) {
		if (record.source.origin != RecordOrigin::MA_FILE || record.account.steamId.has_value() || !record.secrets.deviceId.isEmpty()) {
			return;
		}
		const QRegularExpression fileNamePattern(NODE_STEAMCOMMUNITY_FILE_NAME_PATTERN, QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch fileNameMatch = fileNamePattern.match(record.source.fileName);
		if (!fileNameMatch.hasMatch()) {
			return;
		}
		bool isConverted = false;
		const quint64 steamId = fileNameMatch.captured(1).toULongLong(&isConverted);
		if (!isConverted || !IsIndividualSteamId(steamId)) {
			return;
		}
		record.account.steamId = steamId;
		record.secrets.deviceId = DeriveNodeSteamTotpDeviceId(steamId);
		record.source.origin = RecordOrigin::NODE_STEAMCOMMUNITY_FILE;
	}

	// WinAuth encodes parameter values as HTML forms do, where '+' stands for a space.
	QUrlQuery FormEncodedQuery(const QUrl& link) {
		QString query = link.query(QUrl::FullyEncoded);
		query.replace(FORM_ENCODED_SPACE, ENCODED_SPACE);
		return QUrlQuery(query);
	}

	std::expected<std::optional<AuthenticatorRecord>, ImportError> ParseOtpauthLink(QStringView line, int lineNumber, const QString& fileName) {
		const QString lineReference = QString::number(lineNumber);
		const QUrl link(line.toString(), QUrl::StrictMode);
		if (!link.isValid() || link.scheme().compare(OTPAUTH_SCHEME, Qt::CaseInsensitive) != 0) {
			return Failure(ImportErrorKind::INVALID_CONTENT, lineReference);
		}

		const QString linkType = link.host().toLower();
		QString label = link.path(QUrl::FullyDecoded);
		if (label.startsWith(PATH_SEPARATOR)) {
			label.remove(0, 1);
		}
		QString labelIssuer;
		if (const qsizetype separatorIndex = label.indexOf(LABEL_ISSUER_SEPARATOR); separatorIndex >= 0) {
			labelIssuer = label.first(separatorIndex);
			label = label.sliced(separatorIndex + 1).trimmed();
		}

		const QUrlQuery query = FormEncodedQuery(link);
		const QString issuer = query.queryItemValue(ISSUER_PARAMETER, QUrl::FullyDecoded);
		const bool isSteamIssuer = issuer.compare(STEAM_ISSUER, Qt::CaseInsensitive) == 0 || labelIssuer.compare(STEAM_ISSUER, Qt::CaseInsensitive) == 0;
		const bool isSteamEntry = linkType == STEAM_LINK_TYPE || (linkType == TOTP_LINK_TYPE && isSteamIssuer);
		if (!isSteamEntry) {
			return std::optional<AuthenticatorRecord>();
		}

		const std::optional<QByteArray> sharedSecret = DecodeBase32(query.queryItemValue(SECRET_PARAMETER, QUrl::FullyDecoded));
		if (!sharedSecret.has_value() || sharedSecret->isEmpty()) {
			return Failure(ImportErrorKind::INVALID_CONTENT, lineReference);
		}

		AuthenticatorRecord record;
		const QString data = query.queryItemValue(DATA_PARAMETER, QUrl::FullyDecoded).trimmed();
		if (data.startsWith(QLatin1Char(JSON_OBJECT_START))) {
			const QJsonDocument dataDocument = QJsonDocument::fromJson(data.toUtf8());
			if (!dataDocument.isObject()) {
				return Failure(ImportErrorKind::INVALID_CONTENT, lineReference);
			}
			std::expected<AuthenticatorRecord, ImportError> parsedRecord = RecordFromSteamFields(dataDocument.object(), RecordOrigin::OTPAUTH_URI, fileName);
			if (!parsedRecord.has_value()) {
				return Failure(parsedRecord.error().kind, lineReference);
			}
			record = std::move(*parsedRecord);
			if (!record.secrets.sharedSecret.isEmpty() && record.secrets.sharedSecret != *sharedSecret) {
				return Failure(ImportErrorKind::INCONSISTENT_SECRETS, lineReference);
			}
		} else {
			record = NewRecord(RecordOrigin::OTPAUTH_URI, fileName);
			// Older WinAuth versions stored only the revocation code in this parameter.
			record.secrets.revocationCode = data;
		}
		record.secrets.sharedSecret = *sharedSecret;

		const QString deviceId = query.queryItemValue(DEVICE_ID_PARAMETER, QUrl::FullyDecoded);
		if (!deviceId.isEmpty() && !record.secrets.deviceId.isEmpty() && deviceId != record.secrets.deviceId) {
			return Failure(ImportErrorKind::INCONSISTENT_SECRETS, lineReference);
		}
		if (!deviceId.isEmpty()) {
			record.secrets.deviceId = deviceId;
		}
		if (label != record.account.accountName) {
			record.label = label;
		}
		if (!FitsInAccountFile(record)) {
			return Failure(ImportErrorKind::FILE_TOO_LARGE, lineReference);
		}
		return std::optional<AuthenticatorRecord>(std::move(record));
	}
} // namespace

std::expected<ImportResult, ImportError> ParseMaFile(const QByteArray& fileContents, const QString& fileName, RecordOrigin origin) {
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(WithoutByteOrderMark(fileContents), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return Failure(ImportErrorKind::INVALID_CONTENT, parseError.errorString());
	}
	QJsonObject fields = document.object();
	if (fields.contains(MANIFEST_ENTRIES_KEY) && fields.contains(MANIFEST_ENCRYPTED_KEY)) {
		return Failure(ImportErrorKind::SDA_MANIFEST_SELECTED);
	}

	std::expected<AuthenticatorRecord, ImportError> record = RecordFromSteamFields(std::move(fields), origin, fileName);
	if (!record.has_value()) {
		return std::unexpected(record.error());
	}
	if (record->secrets.sharedSecret.isEmpty()) {
		return Failure(ImportErrorKind::INVALID_CONTENT, SHARED_SECRET_KEY);
	}
	ApplyNodeSteamcommunityFileConventions(*record);
	// Fields of other programs are kept, so the file is refused here rather than failing to save later.
	if (!FitsInAccountFile(*record)) {
		return Failure(ImportErrorKind::FILE_TOO_LARGE);
	}
	ImportResult result;
	result.records.push_back(std::move(*record));
	return result;
}

std::expected<ImportResult, ImportError> ParseOtpauthLinks(QStringView text, const QString& fileName) {
	ImportResult result;
	int lineNumber = 0;
	for (const QStringView line : text.split(u'\n')) {
		++lineNumber;
		const QStringView trimmedLine = line.trimmed();
		if (trimmedLine.isEmpty()) {
			continue;
		}
		if (!trimmedLine.startsWith(OTPAUTH_LINK_PREFIX, Qt::CaseInsensitive)) {
			++result.skippedEntryCount;
			continue;
		}
		std::expected<std::optional<AuthenticatorRecord>, ImportError> record = ParseOtpauthLink(trimmedLine, lineNumber, fileName);
		if (!record.has_value()) {
			return std::unexpected(record.error());
		}
		if (record->has_value()) {
			result.records.push_back(std::move(**record));
		} else {
			++result.skippedEntryCount;
		}
	}
	if (result.records.empty() && result.skippedEntryCount == 0) {
		return Failure(ImportErrorKind::UNRECOGNIZED_FORMAT);
	}
	return result;
}

std::expected<SdaEncryptionParameters, ImportError> FindSdaEncryptionParameters(const QByteArray& manifestContents, const QString& maFileName) {
	const QJsonDocument manifest = QJsonDocument::fromJson(WithoutByteOrderMark(manifestContents));
	if (!manifest.isObject()) {
		return Failure(ImportErrorKind::INVALID_CONTENT, SDA_MANIFEST_FILE_NAME);
	}
	const QJsonArray entries = manifest.object().value(MANIFEST_ENTRIES_KEY).toArray();
	for (const QJsonValue entryValue : entries) {
		const QJsonObject entry = entryValue.toObject();
		if (entry.value(MANIFEST_FILE_NAME_KEY).toString().compare(maFileName, Qt::CaseInsensitive) != 0) {
			continue;
		}
		const std::optional<QByteArray> salt = DecodeBase64(entry.value(MANIFEST_SALT_KEY).toString());
		const std::optional<QByteArray> initializationVector = DecodeBase64(entry.value(MANIFEST_INITIALIZATION_VECTOR_KEY).toString());
		if (!salt.has_value() || salt->isEmpty() || !initializationVector.has_value() || initializationVector->size() != SDA_INITIALIZATION_VECTOR_LENGTH) {
			return Failure(ImportErrorKind::INVALID_CONTENT, SDA_MANIFEST_FILE_NAME);
		}
		return SdaEncryptionParameters{
			.salt = *salt,
			.initializationVector = *initializationVector,
		};
	}
	return Failure(ImportErrorKind::SDA_MANIFEST_ENTRY_MISSING, maFileName);
}

std::expected<QByteArray, ImportError> DecryptSdaMaFile(const QByteArray& encryptedContents, const SdaEncryptionParameters& parameters, const QString& passkey) {
	if (passkey.isEmpty()) {
		return Failure(ImportErrorKind::PASSKEY_REQUIRED);
	}
	const std::optional<QByteArray> ciphertext = DecodeBase64(QString::fromLatin1(encryptedContents.trimmed()));
	if (!ciphertext.has_value()) {
		return Failure(ImportErrorKind::INVALID_CONTENT);
	}
	// .NET Rfc2898DeriveBytes encodes a string password as UTF-8.
	std::optional<QByteArray> key = DeriveKeyPbkdf2HmacSha1(passkey.toUtf8(), parameters.salt, SDA_PBKDF2_ITERATIONS, SDA_KEY_LENGTH);
	if (!key.has_value()) {
		return Failure(ImportErrorKind::INVALID_CONTENT);
	}
	const std::optional<QByteArray> plaintext = DecryptAes256CbcPkcs7(*ciphertext, *key, parameters.initializationVector);
	key->fill('\0');
	// A wrong key almost always breaks the padding; in the rare remaining case the text is not JSON.
	if (!plaintext.has_value() || !WithoutByteOrderMark(*plaintext).trimmed().startsWith(JSON_OBJECT_START)) {
		return Failure(ImportErrorKind::PASSKEY_INCORRECT);
	}
	return *plaintext;
}

std::expected<ImportResult, ImportError> ImportFile(const QString& filePath, const std::optional<QString>& passkey) {
	const QFileInfo fileInfo(filePath);
	const QString fileName = fileInfo.fileName();
	const std::expected<QByteArray, ImportError> contents = ReadLimitedFile(filePath);
	if (!contents.has_value()) {
		return std::unexpected(contents.error());
	}

	if (contents->startsWith(JSON_OBJECT_START)) {
		return ParseMaFile(*contents, fileName);
	}
	const QString text = QString::fromUtf8(*contents);
	if (text.contains(OTPAUTH_LINK_PREFIX, Qt::CaseInsensitive)) {
		return ParseOtpauthLinks(text, fileName);
	}

	// Steam Desktop Authenticator writes an encrypted .maFile as one Base64 block of AES blocks.
	const std::optional<QByteArray> ciphertext = DecodeBase64(text);
	if (!ciphertext.has_value() || ciphertext->isEmpty() || ciphertext->size() % SDA_CIPHER_BLOCK_LENGTH != 0) {
		return Failure(ImportErrorKind::UNRECOGNIZED_FORMAT);
	}
	const QString manifestPath = fileInfo.dir().filePath(SDA_MANIFEST_FILE_NAME);
	if (!QFileInfo::exists(manifestPath)) {
		return Failure(ImportErrorKind::SDA_MANIFEST_MISSING);
	}
	const std::expected<QByteArray, ImportError> manifestContents = ReadLimitedFile(manifestPath);
	if (!manifestContents.has_value()) {
		return std::unexpected(manifestContents.error());
	}
	const std::expected<SdaEncryptionParameters, ImportError> parameters = FindSdaEncryptionParameters(*manifestContents, fileName);
	if (!parameters.has_value()) {
		return std::unexpected(parameters.error());
	}
	if (!passkey.has_value()) {
		return Failure(ImportErrorKind::PASSKEY_REQUIRED);
	}
	const std::expected<QByteArray, ImportError> plaintext = DecryptSdaMaFile(*contents, *parameters, *passkey);
	if (!plaintext.has_value()) {
		return std::unexpected(plaintext.error());
	}
	return ParseMaFile(*plaintext, fileName, RecordOrigin::SDA_ENCRYPTED_MA_FILE);
}

} // namespace SteamForge
