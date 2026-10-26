#include "ui/steam_picture.h"

#include "steam/steam_image_cache.h"

namespace SteamForge {

namespace {
	constexpr qreal CORNER_RADIUS = 3.0;
	constexpr QColor PICTURE_BACKGROUND{0x1b, 0x20, 0x28};
} // namespace

QRectF FittedRect(const QSizeF& pictureSize, const QRectF& bounds) {
	const QSizeF fittedSize = pictureSize.scaled(bounds.size(), Qt::KeepAspectRatio);
	return {bounds.center().x() - (fittedSize.width() / 2), bounds.center().y() - (fittedSize.height() / 2), fittedSize.width(), fittedSize.height()};
}

PictureView::PictureView(int edgeLength, QWidget* parent) :
	QWidget(parent),
	edge(edgeLength) {
	setFixedSize(edge, edge);
}

void PictureView::FillAvailableWidth() {
	setMaximumWidth(QWIDGETSIZE_MAX);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void PictureView::SetPicture(const QImage& image) {
	if (image.cacheKey() == picture.cacheKey()) {
		return;
	}
	picture = image;
	scaledPicture = {};
	update();
}

QSize PictureView::sizeHint() const {
	return {edge, edge};
}

void PictureView::paintEvent(QPaintEvent* /*event*/) {
	QPainter painter(this);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	QPainterPath frame;
	frame.addRoundedRect(QRectF(rect()), CORNER_RADIUS, CORNER_RADIUS);
	painter.fillPath(frame, PICTURE_BACKGROUND);
	if (picture.isNull()) {
		return;
	}
	const QRectF pictureRect = FittedRect(picture.size(), QRectF(rect()));
	const qreal pixelRatio = devicePixelRatioF();
	const QSize pixelSize = (pictureRect.size() * pixelRatio).toSize();
	if (scaledPicture.size() != pixelSize) {
		// A large avatar drawn small with the paint filter alone looks grainy.
		scaledPicture = picture.scaled(pixelSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		scaledPicture.setDevicePixelRatio(pixelRatio);
	}
	painter.setClipPath(frame);
	painter.drawImage(pictureRect, scaledPicture);
}

SteamPicture::SteamPicture(SteamImageCache& images, int edgeLength, QWidget* parent) :
	PictureView(edgeLength, parent),
	imageCache(images) {
	connect(&imageCache, &SteamImageCache::Loaded, this, &SteamPicture::HandleLoaded);
}

void SteamPicture::SetUrl(const QUrl& pictureUrl) {
	if (url == pictureUrl) {
		return;
	}
	url = pictureUrl;
	SetPicture(url.isEmpty() ? QImage() : imageCache.Image(url));
}

void SteamPicture::HandleLoaded(const QUrl& loadedUrl) {
	if (loadedUrl == url) {
		SetPicture(imageCache.Image(url));
	}
}

} // namespace SteamForge
