#pragma once

namespace SteamForge {

// Addresses of the Google sign-in and Drive; tests point them at a local server.
struct GoogleEndpoints {
	QUrl authorizationUrl;
	QUrl tokenUrl;
	QUrl revocationUrl;
	QUrl driveFilesUrl;
	QUrl driveUploadUrl;
};

[[nodiscard]] inline GoogleEndpoints DefaultGoogleEndpoints() {
	return GoogleEndpoints{
		.authorizationUrl = QUrl(QStringLiteral("https://accounts.google.com/o/oauth2/v2/auth")),
		.tokenUrl = QUrl(QStringLiteral("https://oauth2.googleapis.com/token")),
		.revocationUrl = QUrl(QStringLiteral("https://oauth2.googleapis.com/revoke")),
		.driveFilesUrl = QUrl(QStringLiteral("https://www.googleapis.com/drive/v3/files")),
		.driveUploadUrl = QUrl(QStringLiteral("https://www.googleapis.com/upload/drive/v3/files")),
	};
}

} // namespace SteamForge
