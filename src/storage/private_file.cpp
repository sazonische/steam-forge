#include "storage/private_file.h"

namespace SteamForge {

namespace {
#ifndef Q_OS_WIN
	// On Windows access follows the folder ACL; the default location is inside the user profile.
	constexpr QFileDevice::Permissions OWNER_ONLY_FILE_PERMISSIONS = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
	constexpr QFileDevice::Permissions OWNER_ONLY_DIRECTORY_PERMISSIONS = OWNER_ONLY_FILE_PERMISSIONS | QFileDevice::ExeOwner;
#endif

	std::unexpected<StorageError> Failure(StorageErrorKind kind, const QString& filePath, const QString& detail = {}) {
		return std::unexpected(StorageError{
			.kind = kind,
			.filePath = filePath,
			.detail = detail,
		});
	}

	std::expected<void, StorageError> EnsurePrivateDirectory(const QString& directoryPath) {
		if (directoryPath.isEmpty() || !QDir().mkpath(directoryPath)) {
			return Failure(StorageErrorKind::DIRECTORY_UNAVAILABLE, directoryPath);
		}
#ifndef Q_OS_WIN
		QFile::setPermissions(directoryPath, OWNER_ONLY_DIRECTORY_PERMISSIONS);
#endif
		return {};
	}
} // namespace

std::expected<void, StorageError> WritePrivateFile(const QString& filePath, const QByteArray& contents) {
	if (std::expected<void, StorageError> directoryReady = EnsurePrivateDirectory(QFileInfo(filePath).absolutePath()); !directoryReady.has_value()) {
		return directoryReady;
	}
	return WriteOwnerOnlyFile(filePath, contents);
}

std::expected<void, StorageError> WriteOwnerOnlyFile(const QString& filePath, const QByteArray& contents) {
	QSaveFile file(filePath);
	if (!file.open(QIODevice::WriteOnly)) {
		return Failure(StorageErrorKind::WRITE_FAILED, filePath, file.errorString());
	}
#ifndef Q_OS_WIN
	file.setPermissions(OWNER_ONLY_FILE_PERMISSIONS);
#endif
	if (file.write(contents) != contents.size() || !file.commit()) {
		return Failure(StorageErrorKind::WRITE_FAILED, filePath, file.errorString());
	}
	return {};
}

std::expected<QJsonObject, StorageError> ReadJsonObjectFile(const QFileInfo& fileInfo, qint64 maxFileSize) {
	const QString filePath = fileInfo.filePath();
	if (fileInfo.size() > maxFileSize) {
		return Failure(StorageErrorKind::FILE_TOO_LARGE, filePath);
	}
	QFile file(filePath);
	if (!file.open(QIODevice::ReadOnly)) {
		return Failure(StorageErrorKind::READ_FAILED, filePath, file.errorString());
	}
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return Failure(StorageErrorKind::INVALID_JSON, filePath, parseError.errorString());
	}
	return document.object();
}

std::expected<void, StorageError> RemoveFileIfExists(const QString& filePath) {
	QFile file(filePath);
	if (!file.exists()) {
		return {};
	}
	if (!file.remove()) {
		return Failure(StorageErrorKind::REMOVE_FAILED, filePath, file.errorString());
	}
	return {};
}

} // namespace SteamForge
