#pragma once

namespace SteamForge {

class CommunityTransport;
class SteamImageCache;

struct AccountProfile {
	// Empty until Steam has told it.
	QString personaName;
	// Null while it downloads.
	QImage avatar;
};

// Steam names and avatars of the accounts, saved so they show without a connection to Steam. Steam is asked
// again only once the saved profile is a day old.
class AccountProfiles final : public QObject {
	Q_OBJECT

public:
	AccountProfiles(CommunityTransport& transport, SteamImageCache& images, QString cacheFilePath, QObject* parent = nullptr);

	// The "?" avatar without SteamID. A missing or old profile is requested; ProfilesChanged follows.
	[[nodiscard]] AccountProfile Profile(std::optional<quint64> steamId);
	// Profiles of accounts that left SteamForge leave the cache too.
	void KeepOnly(const QSet<quint64>& steamIds);

Q_SIGNALS:
	void ProfilesChanged();

private:
	struct SavedProfile {
		QString personaName;
		QUrl avatarUrl;
		QImage avatar;
		// The avatar can be older than the address until the new picture arrives.
		QUrl avatarPictureUrl;
		QDateTime checkedAt;
	};

	void Load();
	void Save() const;
	void Refresh(quint64 steamId);
	void RequestPicture(const QUrl& pictureUrl);
	void AcceptPicture(const QUrl& pictureUrl, const QImage& picture);

	CommunityTransport& communityTransport;
	SteamImageCache& imageCache;
	QString filePath;
	QHash<quint64, SavedProfile> profiles;
	QImage defaultAvatar;
	// Loading or waiting to try again after a failure.
	QSet<quint64> pendingSteamIds;
	// Announces changes after the current call returns, so Profile() never re-enters its callers.
	QTimer changeNotifier;
};

} // namespace SteamForge
