#include "app/account_profiles.h"

#include "steam/community_profile.h"
#include "steam/community_transport.h"
#include "steam/steam_image_cache.h"
#include "storage/private_file.h"

namespace SteamForge {

namespace {
	// Version 2 keeps the large avatars; a cache of version 1 is simply asked from Steam again.
	constexpr int CACHE_FORMAT_VERSION = 2;
	constexpr qint64 MAX_CACHE_FILE_SIZE = qint64{4} << 20;
	constexpr std::chrono::seconds REFRESH_INTERVAL{std::chrono::hours(24)};
	constexpr std::chrono::milliseconds RETRY_DELAY{std::chrono::minutes(5)};
	constexpr QLatin1StringView VERSION_KEY{"version"};
	constexpr QLatin1StringView DEFAULT_AVATAR_KEY{"defaultAvatar"};
	constexpr QLatin1StringView PROFILES_KEY{"profiles"};
	constexpr QLatin1StringView PERSONA_NAME_KEY{"personaName"};
	constexpr QLatin1StringView AVATAR_URL_KEY{"avatarUrl"};
	constexpr QLatin1StringView AVATAR_KEY{"avatar"};
	constexpr QLatin1StringView AVATAR_PICTURE_URL_KEY{"avatarPictureUrl"};
	constexpr QLatin1StringView CHECKED_AT_KEY{"checkedAt"};
	constexpr QLatin1StringView XML_QUERY_NAME{"xml"};
	constexpr QLatin1StringView XML_QUERY_VALUE{"1"};
	constexpr const char* PICTURE_FORMAT = "PNG";

	QString EncodePicture(const QImage& picture) {
		if (picture.isNull()) {
			return {};
		}
		QByteArray pictureBytes;
		QBuffer buffer(&pictureBytes);
		buffer.open(QIODevice::WriteOnly);
		picture.save(&buffer, PICTURE_FORMAT);
		return QString::fromLatin1(pictureBytes.toBase64());
	}

	QImage DecodePicture(const QJsonValue& value) {
		return QImage::fromData(QByteArray::fromBase64(value.toString().toLatin1()), PICTURE_FORMAT);
	}

	QUrl SavedSteamImageUrl(const QJsonValue& value) {
		const QUrl url(value.toString(), QUrl::StrictMode);
		return SteamImageCache::IsSteamImageUrl(url) ? url : QUrl();
	}

	bool IsFresh(const QDateTime& checkedAt) {
		if (!checkedAt.isValid()) {
			return false;
		}
		// A clock set back makes the time negative: that is not trusted as fresh either.
		const qint64 ageSeconds = checkedAt.secsTo(QDateTime::currentDateTimeUtc());
		return ageSeconds >= 0 && ageSeconds < REFRESH_INTERVAL.count();
	}
} // namespace

AccountProfiles::AccountProfiles(CommunityTransport& transport, SteamImageCache& images, QString cacheFilePath, QObject* parent) :
	QObject(parent),
	communityTransport(transport),
	imageCache(images),
	filePath(std::move(cacheFilePath)) {
	changeNotifier.setSingleShot(true);
	changeNotifier.setInterval(0);
	connect(&changeNotifier, &QTimer::timeout, this, &AccountProfiles::ProfilesChanged);
	connect(&imageCache, &SteamImageCache::Loaded, this, [this](const QUrl& pictureUrl) {
		AcceptPicture(pictureUrl, imageCache.Image(pictureUrl));
	});
	Load();
}

AccountProfile AccountProfiles::Profile(std::optional<quint64> steamId) {
	if (!steamId.has_value() || *steamId == 0) {
		if (defaultAvatar.isNull()) {
			RequestPicture(DefaultAvatarUrl());
		}
		return AccountProfile{
			.personaName = {},
			.avatar = defaultAvatar,
		};
	}
	const auto savedEntry = profiles.constFind(*steamId);
	if (savedEntry == profiles.cend()) {
		Refresh(*steamId);
		return {};
	}
	// Stays valid: a picture that arrives at once only changes values, it adds no profiles.
	const SavedProfile& saved = *savedEntry;
	if (!IsFresh(saved.checkedAt)) {
		Refresh(*steamId);
	}
	if (saved.avatarUrl.isValid() && saved.avatarPictureUrl != saved.avatarUrl) {
		RequestPicture(saved.avatarUrl);
	}
	return AccountProfile{
		.personaName = saved.personaName,
		.avatar = saved.avatar,
	};
}

void AccountProfiles::KeepOnly(const QSet<quint64>& steamIds) {
	pendingSteamIds.intersect(steamIds);
	const qsizetype removedCount = profiles.removeIf([&steamIds](QHash<quint64, SavedProfile>::iterator entry) {
		return !steamIds.contains(entry.key());
	});
	if (removedCount > 0) {
		Save();
	}
}

void AccountProfiles::Load() {
	const QFileInfo fileInfo(filePath);
	if (!fileInfo.exists()) {
		return;
	}
	// Only a cache: when it is damaged or from a newer version, the profiles are asked from Steam again.
	const std::expected<QJsonObject, StorageError> cache = ReadJsonObjectFile(fileInfo, MAX_CACHE_FILE_SIZE);
	if (!cache.has_value() || cache->value(VERSION_KEY).toInt() != CACHE_FORMAT_VERSION) {
		return;
	}
	defaultAvatar = DecodePicture(cache->value(DEFAULT_AVATAR_KEY));
	const QJsonObject savedProfiles = cache->value(PROFILES_KEY).toObject();
	for (auto entry = savedProfiles.constBegin(); entry != savedProfiles.constEnd(); ++entry) {
		bool isNumber = false;
		const quint64 steamId = entry.key().toULongLong(&isNumber);
		if (!isNumber || steamId == 0) {
			continue;
		}
		const QJsonObject saved = entry.value().toObject();
		profiles.insert(steamId, SavedProfile{
									 .personaName = saved.value(PERSONA_NAME_KEY).toString(),
									 .avatarUrl = SavedSteamImageUrl(saved.value(AVATAR_URL_KEY)),
									 .avatar = DecodePicture(saved.value(AVATAR_KEY)),
									 .avatarPictureUrl = SavedSteamImageUrl(saved.value(AVATAR_PICTURE_URL_KEY)),
									 .checkedAt = QDateTime::fromString(saved.value(CHECKED_AT_KEY).toString(), Qt::ISODateWithMs),
								 });
	}
}

void AccountProfiles::Save() const {
	QJsonObject savedProfiles;
	for (auto entry = profiles.constBegin(); entry != profiles.constEnd(); ++entry) {
		const SavedProfile& saved = entry.value();
		savedProfiles.insert(QString::number(entry.key()), QJsonObject{
															   {PERSONA_NAME_KEY, saved.personaName},
															   {AVATAR_URL_KEY, saved.avatarUrl.toString()},
															   {AVATAR_KEY, EncodePicture(saved.avatar)},
															   {AVATAR_PICTURE_URL_KEY, saved.avatarPictureUrl.toString()},
															   {CHECKED_AT_KEY, saved.checkedAt.toString(Qt::ISODateWithMs)},
														   });
	}
	const QJsonObject cache{
		{VERSION_KEY, CACHE_FORMAT_VERSION},
		{DEFAULT_AVATAR_KEY, EncodePicture(defaultAvatar)},
		{PROFILES_KEY, savedProfiles},
	};
	if (!WritePrivateFile(filePath, QJsonDocument(cache).toJson(QJsonDocument::Compact)).has_value()) {
		qWarning("The Steam names and avatars could not be saved.");
	}
}

void AccountProfiles::Refresh(quint64 steamId) {
	if (pendingSteamIds.contains(steamId)) {
		return;
	}
	pendingSteamIds.insert(steamId);
	const CommunityRequest request{
		.path = CommunityProfilePath(steamId),
		.httpMethod = WebApiHttpMethod::GET,
		.parameters = {{XML_QUERY_NAME, XML_QUERY_VALUE}},
		// A public page: no account's sign-in travels with it.
		.session = {},
		.extraCookies = {},
	};
	communityTransport.Send(request, *this, [this, steamId](const WebApiResponse& response) {
		// The account may have left SteamForge meanwhile.
		if (!pendingSteamIds.contains(steamId)) {
			return;
		}
		const std::optional<CommunityProfile> profile = response.has_value() ? ParseCommunityProfile(*response, steamId) : std::nullopt;
		if (!profile.has_value()) {
			// The saved profile stays shown until Steam answers.
			QTimer::singleShot(RETRY_DELAY, this, [this, steamId]() {
				if (pendingSteamIds.remove(steamId)) {
					Refresh(steamId);
				}
			});
			return;
		}
		pendingSteamIds.remove(steamId);
		SavedProfile& saved = profiles[steamId];
		saved.personaName = profile->personaName;
		saved.avatarUrl = profile->avatarUrl;
		saved.checkedAt = QDateTime::currentDateTimeUtc();
		const bool needsPicture = saved.avatarPictureUrl != saved.avatarUrl;
		Save();
		changeNotifier.start();
		if (needsPicture) {
			RequestPicture(profile->avatarUrl);
		}
	});
}

void AccountProfiles::RequestPicture(const QUrl& pictureUrl) {
	// A picture already in memory is taken at once; otherwise SteamImageCache::Loaded follows the download.
	if (const QImage picture = imageCache.Image(pictureUrl); !picture.isNull()) {
		AcceptPicture(pictureUrl, picture);
	}
}

void AccountProfiles::AcceptPicture(const QUrl& pictureUrl, const QImage& picture) {
	if (picture.isNull()) {
		return;
	}
	bool isChanged = false;
	if (pictureUrl == DefaultAvatarUrl() && defaultAvatar.isNull()) {
		defaultAvatar = picture;
		isChanged = true;
	}
	for (SavedProfile& saved : profiles) {
		if (saved.avatarUrl == pictureUrl && saved.avatarPictureUrl != pictureUrl) {
			saved.avatar = picture;
			saved.avatarPictureUrl = pictureUrl;
			isChanged = true;
		}
	}
	if (isChanged) {
		Save();
		changeNotifier.start();
	}
}

} // namespace SteamForge
