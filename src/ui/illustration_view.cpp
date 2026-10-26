#include "ui/illustration_view.h"

namespace SteamForge {

IllustrationView::IllustrationView(QLatin1StringView resourcePath, QSize pictureSize, QWidget* parent) :
	QWidget(parent),
	displaySize(pictureSize) {
	setFixedSize(displaySize);
	SetResource(resourcePath);
}

void IllustrationView::SetResource(QLatin1StringView resourcePath) {
	if (shownResourcePath == resourcePath) {
		return;
	}
	shownResourcePath = resourcePath;
	renderer.load(shownResourcePath);
	Q_ASSERT(renderer.isValid());
	update();
}

QSize IllustrationView::sizeHint() const {
	return displaySize;
}

void IllustrationView::paintEvent(QPaintEvent* /*event*/) {
	QPainter painter(this);
	painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	renderer.render(&painter, QRectF(rect()));
}

} // namespace SteamForge
