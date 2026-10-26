#include "ui/record_presentation.h"

namespace SteamForge {

namespace {
	constexpr QStringView HIDDEN_ACCOUNT_NAME{u"••••••••"};

	QString NativePath(const QString& filePath) {
		return QDir::toNativeSeparators(filePath);
	}
} // namespace

QString RecordDisplayName(const AuthenticatorRecord& record) {
	if (!record.label.isEmpty()) {
		return record.label;
	}
	if (!record.account.accountName.isEmpty()) {
		return record.account.accountName;
	}
	if (record.account.steamId.has_value()) {
		return QString::number(*record.account.steamId);
	}
	return QObject::tr("Unnamed account");
}

bool LeadsWithPersonaName(const AuthenticatorRecord& record, const QString& personaName) {
	return !personaName.isEmpty() && personaName != RecordDisplayName(record);
}

QString AccountTitle(const AuthenticatorRecord& record, const QString& personaName) {
	return LeadsWithPersonaName(record, personaName) ? personaName : RecordDisplayName(record);
}

QString HiddenAccountName() {
	return HIDDEN_ACCOUNT_NAME.toString();
}

QString SteamIdText(const AuthenticatorRecord& record) {
	if (!record.account.steamId.has_value()) {
		return QObject::tr("SteamID unknown");
	}
	return QObject::tr("SteamID %1").arg(*record.account.steamId);
}

QString RecordSourceText(const AuthenticatorRecord& record) {
	const QString date = QLocale().toString(record.source.importedAt.toLocalTime(), QLocale::ShortFormat);
	QString fileOrigin;
	switch (record.source.origin) {
		case RecordOrigin::MA_FILE:
			fileOrigin = QObject::tr("Steam Desktop Authenticator file");
			break;
		case RecordOrigin::SDA_ENCRYPTED_MA_FILE:
			fileOrigin = QObject::tr("encrypted Steam Desktop Authenticator file");
			break;
		case RecordOrigin::NODE_STEAMCOMMUNITY_FILE:
			fileOrigin = QObject::tr("node-steamcommunity file");
			break;
		case RecordOrigin::OTPAUTH_URI:
			fileOrigin = QObject::tr("otpauth link");
			break;
		case RecordOrigin::STEAM_SIGN_IN:
			return QObject::tr("added by signing in to Steam on %1").arg(date);
		case RecordOrigin::ADDED_IN_STEAMFORGE:
			return QObject::tr("Steam Guard set up in SteamForge on %1").arg(date);
		case RecordOrigin::MOVED_TO_STEAMFORGE:
			return QObject::tr("Steam Guard moved to SteamForge on %1").arg(date);
	}
	if (!record.source.fileName.isEmpty()) {
		fileOrigin += QLatin1Char(' ') + record.source.fileName;
	}
	return QObject::tr("imported from %1 on %2").arg(fileOrigin, date);
}

QString StorageErrorText(const StorageError& error) {
	const QString filePath = NativePath(error.filePath);
	switch (error.kind) {
		case StorageErrorKind::DIRECTORY_UNAVAILABLE:
			return QObject::tr("The data folder %1 cannot be created or written.").arg(filePath);
		case StorageErrorKind::READ_FAILED:
			return QObject::tr("%1 could not be read: %2").arg(filePath, error.detail);
		case StorageErrorKind::FILE_TOO_LARGE:
			return QObject::tr("%1 is too large to be an account file.").arg(filePath);
		case StorageErrorKind::INVALID_JSON:
			return QObject::tr("%1 is not valid JSON.").arg(filePath);
		case StorageErrorKind::UNKNOWN_FORMAT:
			return QObject::tr("%1 is not a SteamForge account file.").arg(filePath);
		case StorageErrorKind::NEWER_FORMAT_VERSION:
			return QObject::tr("%1 was saved by a newer version of SteamForge.").arg(filePath);
		case StorageErrorKind::INVALID_CONTENT:
			return QObject::tr("%1 is damaged (field \"%2\").").arg(filePath, error.detail);
		case StorageErrorKind::WRITE_FAILED:
			return QObject::tr("%1 could not be saved: %2").arg(filePath, error.detail);
		case StorageErrorKind::REMOVE_FAILED:
			return QObject::tr("%1 could not be deleted: %2").arg(filePath, error.detail);
		case StorageErrorKind::KEY_UNAVAILABLE:
			return QObject::tr("%1 is encrypted, and its key is not available to this user account on this computer.").arg(filePath);
		case StorageErrorKind::UNSEALING_FAILED:
			return QObject::tr("%1 is encrypted with another key or was changed after encryption.").arg(filePath);
	}
	return {};
}

namespace {
	constexpr int TOO_MANY_REQUESTS_STATUS = 429;
	constexpr int UNAUTHORIZED_STATUS = 401;
	constexpr int FORBIDDEN_STATUS = 403;
	constexpr int FIRST_SERVER_ERROR_STATUS = 500;

	enum class ErrorCause {
		NO_CONNECTION,
		NO_ANSWER,
		STEAM_UNAVAILABLE,
		TOO_MANY_REQUESTS,
		SIGN_IN_REJECTED,
		CLOCK_DIFFERS,
		UNREADABLE_ANSWER,
		REFUSED
	};

	ErrorCause CauseOfSteamResult(SteamResult steamResult) {
		switch (steamResult) {
			case SteamResult::NO_CONNECTION:
			case SteamResult::BUSY:
			case SteamResult::SERVICE_UNAVAILABLE:
				return ErrorCause::STEAM_UNAVAILABLE;
			case SteamResult::TIMEOUT:
				return ErrorCause::NO_ANSWER;
			case SteamResult::LIMIT_EXCEEDED:
			case SteamResult::RATE_LIMIT_EXCEEDED:
			case SteamResult::ACCOUNT_LOGIN_DENIED_THROTTLE:
				return ErrorCause::TOO_MANY_REQUESTS;
			case SteamResult::ACCESS_DENIED:
			case SteamResult::NOT_LOGGED_ON:
			case SteamResult::EXPIRED:
			case SteamResult::REVOKED:
				return ErrorCause::SIGN_IN_REJECTED;
			case SteamResult::TIME_NOT_SYNCED:
				return ErrorCause::CLOCK_DIFFERS;
			default:
				return ErrorCause::REFUSED;
		}
	}

	ErrorCause CauseOf(const WebApiError& error) {
		switch (error.kind) {
			case WebApiErrorKind::NETWORK:
				return ErrorCause::NO_CONNECTION;
			case WebApiErrorKind::TIMEOUT:
				return ErrorCause::NO_ANSWER;
			case WebApiErrorKind::MALFORMED_RESPONSE:
			case WebApiErrorKind::RESPONSE_TOO_LARGE:
				return ErrorCause::UNREADABLE_ANSWER;
			case WebApiErrorKind::NEEDS_AUTHENTICATION:
				return ErrorCause::SIGN_IN_REJECTED;
			case WebApiErrorKind::UNSUCCESSFUL_RESPONSE:
				return ErrorCause::REFUSED;
			case WebApiErrorKind::STEAM_RESULT:
				return CauseOfSteamResult(error.steamResult);
			case WebApiErrorKind::HTTP_STATUS:
				if (error.httpStatus == TOO_MANY_REQUESTS_STATUS) {
					return ErrorCause::TOO_MANY_REQUESTS;
				}
				if (error.httpStatus == UNAUTHORIZED_STATUS || error.httpStatus == FORBIDDEN_STATUS) {
					return ErrorCause::SIGN_IN_REJECTED;
				}
				return error.httpStatus >= FIRST_SERVER_ERROR_STATUS ? ErrorCause::STEAM_UNAVAILABLE : ErrorCause::REFUSED;
		}
		return ErrorCause::REFUSED;
	}

	QString Explanation(ErrorCause cause) {
		switch (cause) {
			case ErrorCause::NO_CONNECTION:
				return QObject::tr("There is no connection to Steam: check the internet connection.");
			case ErrorCause::NO_ANSWER:
				return QObject::tr("Steam did not answer in time.");
			case ErrorCause::STEAM_UNAVAILABLE:
				return QObject::tr("Steam is temporarily unavailable.");
			case ErrorCause::TOO_MANY_REQUESTS:
				return QObject::tr("Steam received too many requests: wait a few minutes.");
			case ErrorCause::SIGN_IN_REJECTED:
				return QObject::tr("Steam no longer accepts the saved sign-in.");
			case ErrorCause::CLOCK_DIFFERS:
				return QObject::tr("The clock of this computer differs from Steam time.");
			case ErrorCause::UNREADABLE_ANSWER:
				return QObject::tr("The answer of Steam could not be read.");
			case ErrorCause::REFUSED:
				return QObject::tr("Steam refused the request.");
		}
		return {};
	}

	QString ErrorCode(const WebApiError& error) {
		switch (error.kind) {
			case WebApiErrorKind::HTTP_STATUS:
				return QStringLiteral("HTTP %1").arg(error.httpStatus);
			case WebApiErrorKind::STEAM_RESULT:
				return QStringLiteral("EResult %1").arg(static_cast<int>(error.steamResult));
			case WebApiErrorKind::NETWORK:
				return error.message;
			default:
				return {};
		}
	}
} // namespace

QString DescribeWebApiError(const QString& whatFailed, const WebApiError& error, const QString& whatNext) {
	QStringList parts{whatFailed, Explanation(CauseOf(error))};
	if (!whatNext.isEmpty()) {
		parts.append(whatNext);
	}
	return WithErrorCode(parts.join(QLatin1Char(' ')), error);
}

QString WithErrorCode(const QString& text, const WebApiError& error) {
	const QString code = ErrorCode(error);
	if (code.isEmpty()) {
		return text;
	}
	return text + QLatin1Char(' ') + QObject::tr("Error code: %1.").arg(code);
}

QString ImportErrorText(const ImportError& error, const QString& fileName) {
	switch (error.kind) {
		case ImportErrorKind::FILE_UNREADABLE:
			return QObject::tr("%1 could not be read: %2").arg(fileName, error.detail);
		case ImportErrorKind::FILE_TOO_LARGE:
			return QObject::tr("%1 is too large to be an authenticator export.").arg(fileName);
		case ImportErrorKind::UNRECOGNIZED_FORMAT:
			return QObject::tr("%1 is neither a .maFile nor a WinAuth export with otpauth:// links.").arg(fileName);
		case ImportErrorKind::INVALID_CONTENT:
			return QObject::tr("%1 is damaged or incomplete (%2). Nothing was imported from it.").arg(fileName, error.detail);
		case ImportErrorKind::INCONSISTENT_SECRETS:
			return QObject::tr("Entry %2 of %1 contains secrets of two different authenticators. Nothing was imported from this file.").arg(fileName, error.detail);
		case ImportErrorKind::SDA_MANIFEST_SELECTED:
			return QObject::tr("%1 is the manifest of Steam Desktop Authenticator. Select the .maFile files from the same folder instead.").arg(fileName);
		case ImportErrorKind::SDA_MANIFEST_MISSING:
			return QObject::tr("%1 is encrypted by Steam Desktop Authenticator, but manifest.json is not in the same folder. Keep the .maFile next to its manifest.json.").arg(fileName);
		case ImportErrorKind::SDA_MANIFEST_ENTRY_MISSING:
			return QObject::tr("manifest.json has no encryption parameters for %1.").arg(fileName);
		case ImportErrorKind::PASSKEY_REQUIRED:
			return QObject::tr("%1 is encrypted. The passkey of Steam Desktop Authenticator is required.").arg(fileName);
		case ImportErrorKind::PASSKEY_INCORRECT:
			return QObject::tr("The passkey does not decrypt %1.").arg(fileName);
	}
	return {};
}

QString ConfirmationTypeText(const PendingConfirmation& confirmation) {
	if (!confirmation.typeName.isEmpty()) {
		return confirmation.typeName;
	}
	switch (confirmation.category) {
		case ConfirmationCategory::TRADE:
			return QObject::tr("Trade offer");
		case ConfirmationCategory::MARKET_LISTING:
			return QObject::tr("Market listing");
		case ConfirmationCategory::OTHER:
			break;
	}
	return QObject::tr("Confirmation");
}

} // namespace SteamForge
