#include "support/render_language.h"

SteamForge::InterfaceLanguage RenderLanguage() {
	if (qEnvironmentVariable("STEAMFORGE_RENDER_LANGUAGE") == QStringLiteral("en")) {
		return SteamForge::InterfaceLanguage::ENGLISH;
	}
	return SteamForge::InterfaceLanguage::RUSSIAN;
}
