#pragma once

namespace SteamForge {

// The device ID node-steam-totp derives from a SteamID (getDeviceID without STEAM_TOTP_SALT).
[[nodiscard]] QString DeriveNodeSteamTotpDeviceId(quint64 steamId);

// A new random device ID in the form Steam Desktop Authenticator uses.
[[nodiscard]] QString GenerateDeviceId();

} // namespace SteamForge
