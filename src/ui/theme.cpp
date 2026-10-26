#include "ui/theme.h"

namespace SteamForge {

namespace {
	constexpr std::array FONT_RESOURCES{
		QLatin1StringView(":/fonts/NotoSans-Regular.ttf"),
		QLatin1StringView(":/fonts/NotoSans-Medium.ttf"),
		QLatin1StringView(":/fonts/NotoSans-Bold.ttf"),
	};
	constexpr QLatin1StringView INTERFACE_FONT_FAMILY{"Noto Sans"};
	constexpr QLatin1StringView STYLE_SHEET_RESOURCE{":/styles/steamforge.qss"};
	constexpr QLatin1StringView BASE_STYLE_NAME{"Fusion"};
	constexpr int DEFAULT_FONT_PIXEL_SIZE = 13;
	constexpr qreal GUARD_CODE_LETTER_SPACING_RATIO = 0.15;

	// Steam client colors (sp.css, library.css); the style sheet uses the same values.
	constexpr QRgb WINDOW_COLOR = qRgb(0x17, 0x1D, 0x25);
	constexpr QRgb DEEP_COLOR = qRgb(0x0E, 0x14, 0x1B);
	constexpr QRgb PANEL_COLOR = qRgb(0x23, 0x26, 0x2E);
	constexpr QRgb BUTTON_COLOR = qRgb(0x3D, 0x44, 0x50);
	constexpr QRgb TEXT_COLOR = qRgb(0xDC, 0xDE, 0xDF);
	constexpr QRgb BRIGHT_TEXT_COLOR = qRgb(0xFF, 0xFF, 0xFF);
	constexpr QRgb MUTED_TEXT_COLOR = qRgb(0x8B, 0x92, 0x9A);
	constexpr QRgb DISABLED_TEXT_COLOR = qRgb(0x67, 0x70, 0x7B);
	constexpr QRgb SELECTED_ROW_COLOR = qRgb(0x3E, 0x4E, 0x69);
	constexpr QRgb LINK_COLOR = qRgb(0x6D, 0xCF, 0xF6);
	constexpr QRgb TOOLTIP_COLOR = qRgb(0xCF, 0xD1, 0xD3);
	constexpr QRgb TOOLTIP_TEXT_COLOR = qRgb(0x17, 0x18, 0x1C);

	void LoadBundledFonts() {
		for (const QLatin1StringView fontResource : FONT_RESOURCES) {
			if (QFontDatabase::addApplicationFont(fontResource) < 0) {
				qWarning("Bundled font %s could not be loaded.", qUtf8Printable(QString(fontResource)));
			}
		}
	}

	QPalette SteamPalette() {
		QPalette palette;
		palette.setColor(QPalette::Window, WINDOW_COLOR);
		palette.setColor(QPalette::WindowText, TEXT_COLOR);
		palette.setColor(QPalette::Base, DEEP_COLOR);
		palette.setColor(QPalette::AlternateBase, PANEL_COLOR);
		palette.setColor(QPalette::Text, TEXT_COLOR);
		palette.setColor(QPalette::PlaceholderText, MUTED_TEXT_COLOR);
		palette.setColor(QPalette::Button, BUTTON_COLOR);
		palette.setColor(QPalette::ButtonText, TEXT_COLOR);
		palette.setColor(QPalette::BrightText, BRIGHT_TEXT_COLOR);
		palette.setColor(QPalette::Highlight, SELECTED_ROW_COLOR);
		palette.setColor(QPalette::HighlightedText, BRIGHT_TEXT_COLOR);
		palette.setColor(QPalette::Link, LINK_COLOR);
		palette.setColor(QPalette::LinkVisited, LINK_COLOR);
		palette.setColor(QPalette::ToolTipBase, TOOLTIP_COLOR);
		palette.setColor(QPalette::ToolTipText, TOOLTIP_TEXT_COLOR);
		for (const QPalette::ColorRole disabledRole : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
			palette.setColor(QPalette::Disabled, disabledRole, DISABLED_TEXT_COLOR);
		}
		return palette;
	}

	QString LoadStyleSheet() {
		QFile styleSheetFile(STYLE_SHEET_RESOURCE);
		if (!styleSheetFile.open(QIODevice::ReadOnly)) {
			qWarning("The application style sheet could not be loaded.");
			return {};
		}
		return QString::fromUtf8(styleSheetFile.readAll());
	}
} // namespace

void ApplyTheme(QApplication& application) {
	LoadBundledFonts();
	QFont interfaceFont(INTERFACE_FONT_FAMILY);
	interfaceFont.setPixelSize(DEFAULT_FONT_PIXEL_SIZE);
	QApplication::setFont(interfaceFont);

	// Also gives the native title bar its dark variant on Windows.
	QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
	QApplication::setStyle(BASE_STYLE_NAME);
	QApplication::setPalette(SteamPalette());
	application.setStyleSheet(LoadStyleSheet());
}

QFont GuardCodeFont(int pixelSize) {
	QFont font(INTERFACE_FONT_FAMILY);
	font.setPixelSize(pixelSize);
	font.setWeight(QFont::Bold);
	font.setLetterSpacing(QFont::AbsoluteSpacing, pixelSize * GUARD_CODE_LETTER_SPACING_RATIO);
	return font;
}

} // namespace SteamForge
