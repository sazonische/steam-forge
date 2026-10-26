#pragma once

#include <QSvgRenderer>

namespace SteamForge {

// A vector picture of the program at a fixed size, sharp at any screen scale.
class IllustrationView final : public QWidget {
public:
	IllustrationView(QLatin1StringView resourcePath, QSize pictureSize, QWidget* parent = nullptr);

	void SetResource(QLatin1StringView resourcePath);
	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QSvgRenderer renderer;
	QString shownResourcePath;
	QSize displaySize;
};

} // namespace SteamForge
