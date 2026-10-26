#pragma once

#include "app/interface_translator.h"

// The language of rendered screens: STEAMFORGE_RENDER_LANGUAGE=en gives English, anything else Russian.
[[nodiscard]] SteamForge::InterfaceLanguage RenderLanguage();
