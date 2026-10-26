#include "storage/authenticator_export.h"

#include "common/encoding.h"

namespace SteamForge {

namespace {
	// Field names of Steam Desktop Authenticator (SteamAuth SteamGuardAccount.cs, SessionData.cs).
	constexpr QLatin1StringView SHARED_SECRET_KEY{"shared_secret"};
	constexpr QLatin1StringView SERIAL_NUMBER_KEY{"serial_number"};
	constexpr QLatin1StringView REVOCATION_CODE_KEY{"revocation_code"};
	constexpr QLatin1StringView URI_KEY{"uri"};
	constexpr QLatin1StringView SERVER_TIME_KEY{"server_time"};
	constexpr QLatin1StringView ACCOUNT_NAME_KEY{"account_name"};
	constexpr QLatin1StringView TOKEN_GID_KEY{"token_gid"};
	constexpr QLatin1StringView IDENTITY_SECRET_KEY{"identity_secret"};
	constexpr QLatin1StringView SECRET_ONE_KEY{"secret_1"};
	constexpr QLatin1StringView STATUS_KEY{"status"};
	constexpr QLatin1StringView DEVICE_ID_KEY{"device_id"};
	constexpr QLatin1StringView FULLY_ENROLLED_KEY{"fully_enrolled"};
	constexpr QLatin1StringView SESSION_KEY{"Session"};
	constexpr QLatin1StringView SESSION_STEAM_ID_KEY{"SteamID"};
	// The uri Steam returns from AddAuthenticator.
	constexpr QLatin1StringView OTPAUTH_URI_PATTERN{"otpauth://totp/Steam:%1?secret=%2&issuer=Steam"};
	constexpr QLatin1StringView MA_FILE_SUFFIX{".maFile"};
	constexpr QLatin1StringView UNSAFE_FILE_NAME_CHARACTERS{R"([^A-Za-z0-9._-])"};
	constexpr QLatin1StringView FALLBACK_FILE_NAME{"account"};
	// The status Steam reports for an authenticator it added; SDA only stores it.
	constexpr int ADDED_AUTHENTICATOR_STATUS = 1;

	QString AccountName(const AuthenticatorRecord& record) {
		return record.account.accountName.isEmpty() ? record.label : record.account.accountName;
	}
} // namespace

QByteArray ExportMaFile(const AuthenticatorRecord& record) {
	// Unknown fields of an imported file travel back unchanged; the known ones come from the record.
	QJsonObject fields = record.additionalFields;
	fields.remove(SESSION_KEY);
	const QString accountName = AccountName(record);
	fields.insert(SHARED_SECRET_KEY, QString::fromLatin1(record.secrets.sharedSecret.toBase64()));
	fields.insert(SERIAL_NUMBER_KEY, record.secrets.serialNumber);
	fields.insert(REVOCATION_CODE_KEY, record.secrets.revocationCode);
	fields.insert(URI_KEY, SteamGuardUri(record));
	fields.insert(ACCOUNT_NAME_KEY, accountName);
	fields.insert(TOKEN_GID_KEY, record.secrets.tokenGid);
	fields.insert(IDENTITY_SECRET_KEY, QString::fromLatin1(record.secrets.identitySecret.toBase64()));
	fields.insert(DEVICE_ID_KEY, record.secrets.deviceId);
	fields.insert(FULLY_ENROLLED_KEY, !record.pendingActivation.has_value());
	if (!fields.contains(SERVER_TIME_KEY)) {
		fields.insert(SERVER_TIME_KEY, 0);
	}
	if (!fields.contains(SECRET_ONE_KEY)) {
		fields.insert(SECRET_ONE_KEY, QString());
	}
	if (!fields.contains(STATUS_KEY)) {
		fields.insert(STATUS_KEY, ADDED_AUTHENTICATOR_STATUS);
	}
	if (record.account.steamId.has_value()) {
		// A JSON integer: a SteamID64 does not fit a double exactly.
		fields.insert(SESSION_KEY, QJsonObject{{SESSION_STEAM_ID_KEY, static_cast<qint64>(*record.account.steamId)}});
	}
	return QJsonDocument(fields).toJson(QJsonDocument::Indented);
}

QString SteamGuardUri(const AuthenticatorRecord& record) {
	return QString(OTPAUTH_URI_PATTERN).arg(QString::fromLatin1(QUrl::toPercentEncoding(AccountName(record))), EncodeBase32(record.secrets.sharedSecret));
}

QString MaFileName(const AuthenticatorRecord& record) {
	if (record.account.steamId.has_value()) {
		return QString::number(*record.account.steamId) + MA_FILE_SUFFIX;
	}
	QString fileName = AccountName(record);
	fileName.replace(QRegularExpression(UNSAFE_FILE_NAME_CHARACTERS), QStringLiteral("_"));
	return (fileName.isEmpty() ? QString(FALLBACK_FILE_NAME) : fileName) + MA_FILE_SUFFIX;
}

} // namespace SteamForge
