#pragma once

namespace SteamForge {

enum class ScreenCaptureFailure {
	NOTHING_CAPTURED,
	PERMISSION_DENIED,
	PORTAL_UNAVAILABLE
};

// Pictures of every screen: Qt screen grabbing on Windows and X11, the xdg-desktop-portal
// Screenshot request in Wayland sessions, where the desktop may ask the user for permission.
class ScreenCapture final : public QObject {
	Q_OBJECT

public:
	explicit ScreenCapture(QObject* parent = nullptr);

	void Capture();

Q_SIGNALS:
	void Captured(const QList<QImage>& images);
	void Failed(ScreenCaptureFailure failure);

private Q_SLOTS:
	void HandlePortalResponse(uint response, const QVariantMap& results);

private:
	void CaptureScreens();
	void RequestPortalScreenshot();

	void FinishCapture();

	QString pendingRequestPath;
	bool isCaptureInProgress = false;
};

} // namespace SteamForge
