#pragma once

namespace SteamForge {

// Steam MobileApp tokens of one record, issued after signing in with the account password.
struct AccountSession {
	QUuid recordId;
	quint64 steamId = 0;
	QString accountName;
	QString refreshToken;
	QString accessToken;
	QDateTime signedInAt;
};

} // namespace SteamForge
