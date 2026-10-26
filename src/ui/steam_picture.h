#pragma once

namespace SteamForge {

class SteamImageCache;

// The largest rectangle of the picture's proportions centred in the bounds.
[[nodiscard]] QRectF FittedRect(const QSizeF& pictureSize, const QRectF& bounds);

// A square picture with rounded corners on a dark background, as Steam shows avatars and items.
class PictureView : public QWidget {
	Q_OBJECT

public:
	explicit PictureView(int edgeLength, QWidget* parent = nullptr);

	// The background takes the whole width of the layout, the picture keeps its height in the centre.
	void FillAvailableWidth();
	void SetPicture(const QImage& image);

	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	int edge = 0;
	QImage picture;
	QImage scaledPicture;
};

// A picture of the Steam content servers that appears once it is downloaded.
class SteamPicture final : public PictureView {
	Q_OBJECT

public:
	SteamPicture(SteamImageCache& images, int edgeLength, QWidget* parent = nullptr);

	void SetUrl(const QUrl& pictureUrl);

private:
	void HandleLoaded(const QUrl& loadedUrl);

	SteamImageCache& imageCache;
	QUrl url;
};

} // namespace SteamForge
