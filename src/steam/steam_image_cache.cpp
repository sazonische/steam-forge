#include "steam/steam_image_cache.h"

#include "steam/steam_http.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView HTTPS_SCHEME{"https"};
	// Hosts the Steam mobile application loads avatars and item pictures from.
	constexpr std::array<QLatin1StringView, 2> STEAM_AKAMAI_HOSTS{QLatin1StringView("steamcommunity-a.akamaihd.net"), QLatin1StringView("steamcdn-a.akamaihd.net")};
	constexpr QLatin1StringView STEAM_STATIC_DOMAIN_SUFFIX{".steamstatic.com"};
	constexpr int BYTES_PER_KIBIBYTE = 1024;
	// The cache counts cost in KiB; this keeps several hundred item pictures.
	constexpr qsizetype MAX_CACHE_KIBIBYTES = qsizetype{64} * BYTES_PER_KIBIBYTE;
	constexpr std::chrono::milliseconds DOWNLOAD_TIMEOUT{std::chrono::seconds(20)};
} // namespace

SteamImageCache::SteamImageCache(QObject* parent) :
	QObject(parent) {
	images.setMaxCost(MAX_CACHE_KIBIBYTES);
}

bool SteamImageCache::IsSteamImageUrl(const QUrl& url) {
	if (!url.isValid() || url.scheme() != HTTPS_SCHEME || !url.userInfo().isEmpty()) {
		return false;
	}
	const QString host = url.host().toLower();
	return host.endsWith(STEAM_STATIC_DOMAIN_SUFFIX) || std::ranges::find(STEAM_AKAMAI_HOSTS, host) != STEAM_AKAMAI_HOSTS.end();
}

QImage SteamImageCache::Image(const QUrl& url) {
	if (const QImage* cachedImage = images.object(url); cachedImage != nullptr) {
		return *cachedImage;
	}
	if (IsSteamImageUrl(url) && !pendingUrls.contains(url) && !failedUrls.contains(url)) {
		Download(url);
	}
	return {};
}

void SteamImageCache::Insert(const QUrl& url, const QImage& image) {
	const auto cost = std::max<qsizetype>(image.sizeInBytes() / BYTES_PER_KIBIBYTE, 1);
	images.insert(url, new QImage(image), cost);
	Q_EMIT Loaded(url);
}

void SteamImageCache::Download(const QUrl& url) {
	pendingUrls.insert(url);
	QNetworkRequest networkRequest(url);
	networkRequest.setTransferTimeout(DOWNLOAD_TIMEOUT);
	networkRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	networkRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
	networkRequest.setMaximumRedirectsAllowed(1);
	QNetworkReply* reply = networkAccessManager.get(networkRequest);
	WatchReply(reply, *this, [this, url](QNetworkReply& finishedReply) {
		pendingUrls.remove(url);
		const QImage image = ReplyFailure(finishedReply).has_value() ? QImage() : QImage::fromData(finishedReply.readAll());
		if (image.isNull()) {
			// Not asked again in this run: a broken picture would otherwise be downloaded on every repaint.
			failedUrls.insert(url);
			return;
		}
		Insert(url, image);
	});
}

} // namespace SteamForge
