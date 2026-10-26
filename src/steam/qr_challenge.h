#pragma once

namespace SteamForge {

// The sign-in request a Steam login window shows as a QR code: https://s.team/q/<version>/<client id>.
struct QrChallenge {
	int version = 0;
	quint64 clientId = 0;

	bool operator==(const QrChallenge&) const = default;
};

[[nodiscard]] std::optional<QrChallenge> ParseQrChallengeUrl(QStringView url);

// HMAC-SHA256 with the shared secret over version (uint16), client ID and SteamID (uint64), little endian.
[[nodiscard]] QByteArray SignQrChallenge(const QrChallenge& challenge, quint64 steamId, QByteArrayView sharedSecret);

// All distinct Steam sign-in requests visible in the image; other QR codes are ignored.
[[nodiscard]] std::vector<QrChallenge> FindQrChallenges(const QImage& image);

} // namespace SteamForge
