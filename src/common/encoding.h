#pragma once

namespace SteamForge {

[[nodiscard]] std::optional<QByteArray> DecodeBase64(QStringView encoded);

// Lenient like otpauth readers: case-insensitive, padding and spaces ignored.
[[nodiscard]] std::optional<QByteArray> DecodeBase32(QStringView encoded);

// The RFC 4648 alphabet without padding, as otpauth links carry secrets.
[[nodiscard]] QString EncodeBase32(const QByteArray& bytes);

} // namespace SteamForge
