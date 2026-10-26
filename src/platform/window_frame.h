#pragma once

namespace SteamForge {

// Gives native title bars the solid interface color instead of the system color and translucent backdrop.
// Has an effect on Windows 11; elsewhere the window manager draws the frame.
void InstallWindowFrameStyling(QApplication& application);

} // namespace SteamForge
