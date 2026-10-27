#include "storage/session_store.h"

#include "domain/authenticator_record.h"
#include "storage/private_file.h"
#include "storage/sealed_file.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView SESSION_FILE_SUFFIX{".json"};
	constexpr QLatin1StringView SESSION_FILE_PATTERN{"*.json"};
	constexpr qint64 MAX_SESSION_DOCUMENT_SIZE = qint64{64} << 10;
	constexpr QLatin1StringView FORMAT_NAME{"steamforge-session"};
	constexpr int SESSION_FORMAT_VERSION = 1;

	constexpr QLatin1StringView FORMAT_KEY{"format"};
	constexpr QLatin1StringView FORMAT_VERSION_KEY{"formatVersion"};
	constexpr QLatin1StringView RECORD_ID_KEY{"recordId"};
	constexpr QLatin1StringView STEAM_ID_KEY{"steamId"};
	constexpr QLatin1StringView ACCOUNT_NAME_KEY{"accountName"};
	constexpr QLatin1StringView REFRESH_TOKEN_KEY{"refreshToken"};
	constexpr QLatin1StringView ACCESS_TOKEN_KEY{"accessToken"};
	constexpr QLatin1StringView SIGNED_IN_AT_KEY{"signedInAt"};

	QJsonObject SerializeSession(const AccountSession& session) {
		return {
			{FORMAT_KEY, FORMAT_NAME},
			{FORMAT_VERSION_KEY, SESSION_FORMAT_VERSION},
			{RECORD_ID_KEY, session.recordId.toString(QUuid::WithoutBraces)},
			{STEAM_ID_KEY, QString::number(session.steamId)},
			{ACCOUNT_NAME_KEY, session.accountName},
			{REFRESH_TOKEN_KEY, session.refreshToken},
			{ACCESS_TOKEN_KEY, session.accessToken},
			{SIGNED_IN_AT_KEY, session.signedInAt.toUTC().toString(Qt::ISODateWithMs)},
		};
	}

	std::expected<AccountSession, StorageError> DeserializeSession(const QJsonObject& document, const QFileInfo& fileInfo) {
		const auto invalid = [&fileInfo](QLatin1StringView fieldName, StorageErrorKind kind = StorageErrorKind::INVALID_CONTENT) {
			return std::unexpected(StorageError{
				.kind = kind,
				.filePath = fileInfo.filePath(),
				.detail = fieldName,
			});
		};
		if (document.value(FORMAT_KEY).toString() != FORMAT_NAME) {
			return invalid(FORMAT_KEY, StorageErrorKind::UNKNOWN_FORMAT);
		}
		if (document.value(FORMAT_VERSION_KEY).toInteger() > SESSION_FORMAT_VERSION) {
			return invalid(FORMAT_VERSION_KEY, StorageErrorKind::NEWER_FORMAT_VERSION);
		}

		AccountSession session;
		session.recordId = QUuid::fromString(document.value(RECORD_ID_KEY).toString());
		if (session.recordId.isNull() || session.recordId.toString(QUuid::WithoutBraces) != fileInfo.completeBaseName()) {
			return invalid(RECORD_ID_KEY);
		}
		bool isSteamIdValid = false;
		session.steamId = document.value(STEAM_ID_KEY).toString().toULongLong(&isSteamIdValid);
		if (!isSteamIdValid || !IsIndividualSteamId(session.steamId)) {
			return invalid(STEAM_ID_KEY);
		}
		session.refreshToken = document.value(REFRESH_TOKEN_KEY).toString();
		if (session.refreshToken.isEmpty()) {
			return invalid(REFRESH_TOKEN_KEY);
		}
		session.accountName = document.value(ACCOUNT_NAME_KEY).toString();
		session.accessToken = document.value(ACCESS_TOKEN_KEY).toString();
		session.signedInAt = QDateTime::fromString(document.value(SIGNED_IN_AT_KEY).toString(), Qt::ISODateWithMs);
		return session;
	}
} // namespace

SessionStore::SessionStore(QString sessionsDirectoryPath) :
	directoryPath(std::move(sessionsDirectoryPath)) {
}

void SessionStore::SetDataKey(const std::optional<DataKey>& key) {
	dataKey = key;
}

SessionLoadResult SessionStore::LoadAll() const {
	SessionLoadResult result;
	const QDir directory(directoryPath);
	if (!directory.exists()) {
		return result;
	}
	for (const QFileInfo& sessionFile : directory.entryInfoList({SESSION_FILE_PATTERN}, QDir::Files, QDir::Name)) {
		const std::expected<QJsonObject, StorageError> document = ReadStoredDocument(sessionFile, MAX_SESSION_DOCUMENT_SIZE, dataKey, SealedFileKind::SESSION);
		if (!document.has_value()) {
			result.problems.push_back(document.error());
			continue;
		}
		std::expected<AccountSession, StorageError> session = DeserializeSession(*document, sessionFile);
		if (session.has_value()) {
			result.sessions.push_back(std::move(*session));
		} else {
			result.problems.push_back(session.error());
		}
	}
	return result;
}

std::expected<void, StorageError> SessionStore::Save(const AccountSession& session) const {
	const QString filePath = SessionFilePath(session.recordId);
	const std::expected<QByteArray, StorageError> contents = EncodeStoredDocument(SerializeSession(session), MAX_SESSION_DOCUMENT_SIZE, dataKey, SealedFileKind::SESSION, filePath);
	if (!contents.has_value()) {
		return std::unexpected(contents.error());
	}
	return WritePrivateFile(filePath, *contents);
}

std::expected<void, StorageError> SessionStore::Remove(const QUuid& recordId) const {
	return RemoveFileIfExists(SessionFilePath(recordId));
}

QString SessionStore::SessionFilePath(const QUuid& recordId) const {
	return QDir(directoryPath).filePath(recordId.toString(QUuid::WithoutBraces) + SESSION_FILE_SUFFIX);
}

} // namespace SteamForge
