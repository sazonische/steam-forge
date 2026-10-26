#pragma once

#include "app/interface_translator.h"

namespace SteamForge {

// An INI file in the data folder, so a portable copy keeps its settings.
class ApplicationSettings final {
public:
	explicit ApplicationSettings(const QString& filePath);

	// Empty means: follow the system language.
	[[nodiscard]] std::optional<InterfaceLanguage> Language() const;
	void SetLanguage(std::optional<InterfaceLanguage> language);

	[[nodiscard]] QByteArray MainWindowGeometry() const;
	void SetMainWindowGeometry(const QByteArray& geometry);

private:
	QSettings settings;
};

} // namespace SteamForge
