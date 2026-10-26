#pragma once

namespace SteamForge {

// A one-line label that shortens its text with an ellipsis instead of widening its parent.
class ElidedLabel final : public QLabel {
	Q_OBJECT

public:
	explicit ElidedLabel(QWidget* parent = nullptr);

	void SetFullText(const QString& text);
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QSize minimumSizeHint() const override;

protected:
	void resizeEvent(QResizeEvent* event) override;

private:
	void UpdateElision();

	QString fullText;
};

} // namespace SteamForge
