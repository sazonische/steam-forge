#pragma once

namespace SteamForge {

// Claims of a Steam JWT read without verifying its signature; only Steam can tell whether it accepts the token.
struct JwtClaims {
	quint64 steamId = 0;
	QStringList audiences;
	std::optional<QDateTime> expiresAt;
};

[[nodiscard]] std::optional<JwtClaims> ReadJwtClaims(QStringView token);

[[nodiscard]] bool HasAudience(const JwtClaims& claims, QLatin1StringView audience);

// True only for a readable token whose expiration has passed; Steam decides about every other token.
[[nodiscard]] bool HasExpired(QStringView token, const QDateTime& moment);

} // namespace SteamForge
