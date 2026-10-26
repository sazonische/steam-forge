#include "platform/data_location.h"

namespace SteamForge {

namespace {
	// Same marker convention as KeePassXC.
	constexpr QLatin1StringView PORTABLE_MARKER_FILE_NAME{".portable"};
	constexpr QLatin1StringView PORTABLE_DATA_DIRECTORY_NAME{"SteamForgeData"};
} // namespace

DataLocation ResolveDataLocation() {
	const QDir executableDirectory(QCoreApplication::applicationDirPath());
	if (QFileInfo::exists(executableDirectory.filePath(PORTABLE_MARKER_FILE_NAME))) {
		return {
			.directoryPath = executableDirectory.filePath(PORTABLE_DATA_DIRECTORY_NAME),
			.isPortable = true,
		};
	}
	return {
		.directoryPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
		.isPortable = false,
	};
}

} // namespace SteamForge
