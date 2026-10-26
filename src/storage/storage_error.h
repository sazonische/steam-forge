#pragma once

namespace SteamForge {

enum class StorageErrorKind {
	DIRECTORY_UNAVAILABLE,
	READ_FAILED,
	FILE_TOO_LARGE,
	INVALID_JSON,
	UNKNOWN_FORMAT,
	NEWER_FORMAT_VERSION,
	INVALID_CONTENT,
	WRITE_FAILED,
	REMOVE_FAILED,
	// Sealed with a data key this computer does not have now.
	KEY_UNAVAILABLE,
	// Sealed with another key, or changed after sealing.
	UNSEALING_FAILED
};

struct StorageError {
	StorageErrorKind kind = StorageErrorKind::INVALID_CONTENT;
	QString filePath;
	// System error text or the name of the offending field.
	QString detail;
};

} // namespace SteamForge
