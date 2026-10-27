#include "ui/elided_label.h"

namespace SteamForge {

ElidedLabel::ElidedLabel(QWidget* parent) :
	QLabel(parent) {
	// Names come from Steam and from imported files and are never interpreted as rich text.
	setTextFormat(Qt::PlainText);
	setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
}

void ElidedLabel::SetFullText(const QString& text) {
	fullText = text;
	updateGeometry();
	UpdateElision();
}

QSize ElidedLabel::sizeHint() const {
	// Based on the full text, so replacing the shown text while resizing never changes the layout. Rounded up: with
	// fractional glyph widths, as on Linux, the rounded width is short of the text and would cut its last letter.
	const QMargins margins = contentsMargins();
	const auto textWidth = static_cast<int>(std::ceil(QFontMetricsF(font()).horizontalAdvance(fullText)));
	return {textWidth + margins.left() + margins.right(), QLabel::sizeHint().height()};
}

QSize ElidedLabel::minimumSizeHint() const {
	return {0, QLabel::minimumSizeHint().height()};
}

void ElidedLabel::resizeEvent(QResizeEvent* event) {
	QLabel::resizeEvent(event);
	UpdateElision();
}

void ElidedLabel::UpdateElision() {
	const QString shownText = fontMetrics().elidedText(fullText, Qt::ElideRight, contentsRect().width());
	setText(shownText);
	setToolTip(shownText == fullText ? QString() : fullText);
}

} // namespace SteamForge
