#pragma once

namespace SteamForge {

[[nodiscard]] QIcon ApplicationIcon();

[[nodiscard]] QIcon TrayIcon();

// Small sizes (title bar) use the tray artwork, large sizes (taskbar, task switcher) the application icon.
[[nodiscard]] QIcon WindowIcon();

} // namespace SteamForge
