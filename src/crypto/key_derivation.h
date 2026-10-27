#pragma once

namespace SteamForge {

// HKDF-SHA256 (RFC 5869) for key material that is already random, such as a recovery key; not for passwords.
[[nodiscard]] std::optional<QByteArray> DeriveKeyBytes(QByteArrayView keyMaterial, QByteArrayView salt, QByteArrayView info, qsizetype length);

} // namespace SteamForge
