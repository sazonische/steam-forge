#pragma once

namespace SteamForge {

// What the public community profile tells about an account.
struct CommunityProfile {
	QString personaName;
	QUrl avatarUrl;
};

// The Steam "?" avatar, shown for an account without SteamID or a profile without an avatar.
[[nodiscard]] QUrl DefaultAvatarUrl();

// The path of the public profile; with the query xml=1 Steam answers in XML and needs no sign-in.
[[nodiscard]] QString CommunityProfilePath(quint64 steamId);

// A profile Steam does not know gives an empty name and the "?" avatar; nothing when the answer is not the
// profile of this SteamID.
[[nodiscard]] std::optional<CommunityProfile> ParseCommunityProfile(QByteArrayView profileXml, quint64 steamId);

} // namespace SteamForge
