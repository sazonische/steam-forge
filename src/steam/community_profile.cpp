#include "steam/community_profile.h"

#include "steam/steam_image_cache.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView PROFILE_PATH_PATTERN{"profiles/%1/"};
	// DEFAULT_AVATAR_HASH and BuildAvatarURLFromHash of the Steam mobile application 3.10.9.
	constexpr QLatin1StringView DEFAULT_AVATAR_URL{"https://avatars.steamstatic.com/fef49e7fa7e1997310d705b2a6158ff8dc1cdfeb_full.jpg"};
	constexpr QLatin1StringView PROFILE_ELEMENT{"profile"};
	// Steam answers an unknown profile with <response><error>.
	constexpr QLatin1StringView ERROR_RESPONSE_ELEMENT{"response"};
	constexpr QLatin1StringView STEAM_ID_ELEMENT{"steamID64"};
	constexpr QLatin1StringView PERSONA_NAME_ELEMENT{"steamID"};
	// 184 pixels: sharp in the panel header as well.
	constexpr QLatin1StringView FULL_AVATAR_ELEMENT{"avatarFull"};
	// Steam allows 32 characters; the margin keeps names Steam may lengthen later.
	constexpr qsizetype MAX_PERSONA_NAME_LENGTH = 64;

	// The owner chooses the name: control and invisible format characters, such as a right-to-left
	// override, could make it look like another account's name.
	QString CleanPersonaName(const QString& name) {
		QString cleanName;
		for (const QChar character : name) {
			if (character.category() != QChar::Other_Control && character.category() != QChar::Other_Format) {
				cleanName.append(character);
			}
		}
		return cleanName.trimmed().left(MAX_PERSONA_NAME_LENGTH);
	}
} // namespace

QUrl DefaultAvatarUrl() {
	return QUrl(DEFAULT_AVATAR_URL);
}

QString CommunityProfilePath(quint64 steamId) {
	return QString(PROFILE_PATH_PATTERN).arg(steamId);
}

std::optional<CommunityProfile> ParseCommunityProfile(QByteArrayView profileXml, quint64 steamId) {
	QXmlStreamReader reader(profileXml);
	if (!reader.readNextStartElement()) {
		return std::nullopt;
	}
	if (reader.name() == ERROR_RESPONSE_ELEMENT) {
		return CommunityProfile{
			.personaName = {},
			.avatarUrl = DefaultAvatarUrl(),
		};
	}
	if (reader.name() != PROFILE_ELEMENT) {
		return std::nullopt;
	}
	std::optional<quint64> profileSteamId;
	CommunityProfile profile{
		.personaName = {},
		.avatarUrl = DefaultAvatarUrl(),
	};
	// Only direct children of <profile>: the groups listed further down carry avatars and names of their own.
	while (reader.readNextStartElement()) {
		if (reader.name() == STEAM_ID_ELEMENT) {
			bool isNumber = false;
			const quint64 parsedSteamId = reader.readElementText().trimmed().toULongLong(&isNumber);
			profileSteamId = isNumber ? std::optional(parsedSteamId) : std::nullopt;
		} else if (reader.name() == PERSONA_NAME_ELEMENT) {
			profile.personaName = CleanPersonaName(reader.readElementText());
		} else if (reader.name() == FULL_AVATAR_ELEMENT) {
			const QUrl avatarUrl(reader.readElementText().trimmed(), QUrl::StrictMode);
			if (SteamImageCache::IsSteamImageUrl(avatarUrl)) {
				profile.avatarUrl = avatarUrl;
			}
		} else {
			reader.skipCurrentElement();
		}
	}
	if (reader.hasError() || profileSteamId != steamId) {
		return std::nullopt;
	}
	return profile;
}

} // namespace SteamForge
