#pragma once

namespace SteamForge {

struct DataLocation {
	QString directoryPath;
	bool isPortable = false;
};

[[nodiscard]] DataLocation ResolveDataLocation();

} // namespace SteamForge
