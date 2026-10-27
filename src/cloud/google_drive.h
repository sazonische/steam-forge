#pragma once

#include "cloud/google_access.h"

namespace SteamForge {

using DriveProperties = QHash<QString, QString>;

struct DriveFile {
	QString id;
	QString name;
	DriveProperties appProperties;
	QDateTime createdTime;
};

// Files of the hidden application folder of Google Drive (appDataFolder). Only SteamForge sees them; the user can
// still delete the whole folder in the Drive settings, so an empty folder is a normal answer, not an error.
class GoogleDrive final : public QObject {
	Q_OBJECT

public:
	GoogleDrive(GoogleTransport& transport, GoogleAccess& access, GoogleEndpoints endpoints, QObject* parent = nullptr);

	// Every file of the folder, oldest first.
	void ListFiles(QObject& context, GoogleHandler<std::vector<DriveFile>> handler);
	void Download(const QString& fileId, QObject& context, GoogleHandler<QByteArray> handler);
	void Create(const QString& name, const DriveProperties& appProperties, const QByteArray& content, QObject& context, GoogleHandler<DriveFile> handler);
	void Delete(const QString& fileId, QObject& context, GoogleHandler<void> handler);

private:
	void ListPage(const QString& pageToken, std::vector<DriveFile> collectedFiles, QObject& context, GoogleHandler<std::vector<DriveFile>> handler);
	// Adds the access token; when Google refuses it, gets a new one and sends the request once more.
	void SendAuthorized(GoogleRequest request, QObject& context, GoogleResponseHandler handler, bool isRepeated = false);
	[[nodiscard]] QUrl FileUrl(const QString& fileId) const;

	GoogleTransport& googleTransport;
	GoogleAccess& googleAccess;
	GoogleEndpoints googleEndpoints;
};

} // namespace SteamForge
