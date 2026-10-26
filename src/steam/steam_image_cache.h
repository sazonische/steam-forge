#pragma once

namespace SteamForge {

// Pictures of the Steam content servers (avatars and items), kept in memory while the program runs.
class SteamImageCache final : public QObject {
	Q_OBJECT

public:
	explicit SteamImageCache(QObject* parent = nullptr);

	// Only HTTPS pictures of the Steam content servers are loaded.
	[[nodiscard]] static bool IsSteamImageUrl(const QUrl& url);

	// The cached picture, or a null image while it downloads; Loaded follows when it arrives.
	[[nodiscard]] QImage Image(const QUrl& url);
	void Insert(const QUrl& url, const QImage& image);

Q_SIGNALS:
	void Loaded(const QUrl& url);

private:
	void Download(const QUrl& url);

	QNetworkAccessManager networkAccessManager;
	QCache<QUrl, QImage> images;
	QSet<QUrl> pendingUrls;
	QSet<QUrl> failedUrls;
};

} // namespace SteamForge
