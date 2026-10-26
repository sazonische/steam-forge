#pragma once

namespace SteamForge {

inline constexpr std::chrono::seconds GUARD_CODE_PERIOD{30};

[[nodiscard]] QString GenerateGuardCode(QByteArrayView sharedSecret, std::chrono::sys_seconds steamTime);

} // namespace SteamForge
