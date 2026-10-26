#include "platform/window_frame.h"

#ifdef Q_OS_WIN
	#include <dwmapi.h>
#endif

namespace SteamForge {

#ifdef Q_OS_WIN
namespace {
	constexpr QRgb TITLE_BAR_COLOR = qRgb(0x17, 0x1D, 0x25);
	constexpr QRgb TITLE_TEXT_COLOR = qRgb(0xDC, 0xDE, 0xDF);
	constexpr const char* FRAME_STYLED_PROPERTY = "steamForgeFrameStyled";

	constexpr COLORREF ToColorReference(QRgb color) {
		return RGB(qRed(color), qGreen(color), qBlue(color));
	}

	template<typename Value>
	void SetWindowAttribute(HWND windowHandle, DWORD attribute, const Value& value) {
		// Older Windows versions reject attributes they do not know and keep the default frame.
		DwmSetWindowAttribute(windowHandle, attribute, &value, sizeof(value));
	}

	void StyleTitleBar(QWidget& window) {
		// WId is the integer form of the native HWND.
		auto* const windowHandle = reinterpret_cast<HWND>(window.winId()); // NOLINT(performance-no-int-to-ptr)
		SetWindowAttribute(windowHandle, DWMWA_SYSTEMBACKDROP_TYPE, DWMSBT_NONE);
		SetWindowAttribute(windowHandle, DWMWA_CAPTION_COLOR, ToColorReference(TITLE_BAR_COLOR));
		SetWindowAttribute(windowHandle, DWMWA_BORDER_COLOR, ToColorReference(TITLE_BAR_COLOR));
		SetWindowAttribute(windowHandle, DWMWA_TEXT_COLOR, ToColorReference(TITLE_TEXT_COLOR));
	}

	class WindowFrameStyler final : public QObject {
	public:
		using QObject::QObject;

	protected:
		bool eventFilter(QObject* watched, QEvent* event) override {
			if (event->type() == QEvent::Show) {
				auto* window = qobject_cast<QWidget*>(watched);
				if (window != nullptr && window->isWindow() && !window->property(FRAME_STYLED_PROPERTY).toBool()) {
					window->setProperty(FRAME_STYLED_PROPERTY, true);
					StyleTitleBar(*window);
				}
			}
			return QObject::eventFilter(watched, event);
		}
	};
} // namespace
#endif

void InstallWindowFrameStyling(QApplication& application) {
#ifdef Q_OS_WIN
	application.installEventFilter(new WindowFrameStyler(&application));
#else
	Q_UNUSED(application);
#endif
}

} // namespace SteamForge
