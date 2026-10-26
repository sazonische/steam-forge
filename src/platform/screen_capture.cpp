#include "platform/screen_capture.h"

#ifdef Q_OS_LINUX
	#include <QtDBus>
#endif

namespace SteamForge {

namespace {
#ifdef Q_OS_LINUX
	constexpr QLatin1StringView PORTAL_SERVICE{"org.freedesktop.portal.Desktop"};
	constexpr QLatin1StringView PORTAL_PATH{"/org/freedesktop/portal/desktop"};
	constexpr QLatin1StringView SCREENSHOT_INTERFACE{"org.freedesktop.portal.Screenshot"};
	constexpr QLatin1StringView SCREENSHOT_METHOD{"Screenshot"};
	constexpr QLatin1StringView REQUEST_INTERFACE{"org.freedesktop.portal.Request"};
	constexpr QLatin1StringView RESPONSE_SIGNAL{"Response"};
	constexpr QLatin1StringView REQUEST_PATH_PATTERN{"/org/freedesktop/portal/desktop/request/%1/%2"};
	constexpr QLatin1StringView HANDLE_TOKEN_OPTION{"handle_token"};
	constexpr QLatin1StringView INTERACTIVE_OPTION{"interactive"};
	constexpr QLatin1StringView URI_RESULT{"uri"};
	constexpr QLatin1StringView HANDLE_TOKEN_PREFIX{"steamforge"};
	constexpr uint PORTAL_SUCCESS = 0;
	constexpr uint PORTAL_CANCELLED_BY_USER = 1;

	bool IsWaylandSession() {
		return qEnvironmentVariable("XDG_SESSION_TYPE") == QLatin1StringView("wayland") || qEnvironmentVariableIsSet("WAYLAND_DISPLAY");
	}

	// The portal announces its answer on a request object whose path follows from the caller's bus name and token.
	QString ExpectedRequestPath(const QString& handleToken) {
		QString senderName = QDBusConnection::sessionBus().baseService();
		senderName.remove(0, 1);
		senderName.replace(QLatin1Char('.'), QLatin1Char('_'));
		return QString(REQUEST_PATH_PATTERN).arg(senderName, handleToken);
	}
#endif
} // namespace

ScreenCapture::ScreenCapture(QObject* parent) :
	QObject(parent) {
}

void ScreenCapture::Capture() {
	if (isCaptureInProgress) {
		return;
	}
	isCaptureInProgress = true;
#ifdef Q_OS_LINUX
	if (IsWaylandSession()) {
		RequestPortalScreenshot();
		return;
	}
#endif
	// Queued, so callers see the same asynchronous behaviour on every platform.
	QTimer::singleShot(0, this, &ScreenCapture::CaptureScreens);
}

void ScreenCapture::CaptureScreens() {
	QList<QImage> images;
	for (QScreen* screen : QGuiApplication::screens()) {
		const QImage image = screen->grabWindow(0).toImage();
		if (!image.isNull()) {
			images.append(image);
		}
	}
	FinishCapture();
	if (images.isEmpty()) {
		Q_EMIT Failed(ScreenCaptureFailure::NOTHING_CAPTURED);
		return;
	}
	Q_EMIT Captured(images);
}

void ScreenCapture::RequestPortalScreenshot() {
#ifdef Q_OS_LINUX
	QDBusConnection sessionBus = QDBusConnection::sessionBus();
	if (!sessionBus.isConnected()) {
		QTimer::singleShot(0, this, [this]() {
			FinishCapture();
			Q_EMIT Failed(ScreenCaptureFailure::PORTAL_UNAVAILABLE);
		});
		return;
	}
	const QString handleToken = HANDLE_TOKEN_PREFIX + QUuid::createUuid().toString(QUuid::Id128);
	pendingRequestPath = ExpectedRequestPath(handleToken);
	sessionBus.connect(PORTAL_SERVICE, pendingRequestPath, REQUEST_INTERFACE, RESPONSE_SIGNAL, this, SLOT(HandlePortalResponse(uint, QVariantMap)));

	QDBusMessage screenshotCall = QDBusMessage::createMethodCall(PORTAL_SERVICE, PORTAL_PATH, SCREENSHOT_INTERFACE, SCREENSHOT_METHOD);
	const QVariantMap options{
		{HANDLE_TOKEN_OPTION, handleToken},
		{INTERACTIVE_OPTION, false},
	};
	screenshotCall << QString() << options;
	auto* callWatcher = new QDBusPendingCallWatcher(sessionBus.asyncCall(screenshotCall), this);
	connect(callWatcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* finishedWatcher) {
		finishedWatcher->deleteLater();
		if (finishedWatcher->isError()) {
			FinishCapture();
			Q_EMIT Failed(ScreenCaptureFailure::PORTAL_UNAVAILABLE);
		}
	});
#endif
}

void ScreenCapture::HandlePortalResponse(uint response, const QVariantMap& results) {
#ifdef Q_OS_LINUX
	QDBusConnection::sessionBus().disconnect(PORTAL_SERVICE, pendingRequestPath, REQUEST_INTERFACE, RESPONSE_SIGNAL, this, SLOT(HandlePortalResponse(uint, QVariantMap)));
#endif
	FinishCapture();
#ifdef Q_OS_LINUX
	if (response == PORTAL_CANCELLED_BY_USER) {
		Q_EMIT Failed(ScreenCaptureFailure::PERMISSION_DENIED);
		return;
	}
	const QImage image(QUrl(results.value(URI_RESULT).toString()).toLocalFile());
	if (response != PORTAL_SUCCESS || image.isNull()) {
		Q_EMIT Failed(ScreenCaptureFailure::NOTHING_CAPTURED);
		return;
	}
	Q_EMIT Captured({image});
#else
	Q_UNUSED(response);
	Q_UNUSED(results);
#endif
}

void ScreenCapture::FinishCapture() {
	isCaptureInProgress = false;
	pendingRequestPath.clear();
}

} // namespace SteamForge
