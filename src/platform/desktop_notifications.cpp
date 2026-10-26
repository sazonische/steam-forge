#include "platform/desktop_notifications.h"

#include "common/constants.h"

#ifdef Q_OS_LINUX
	#include <QtDBus>
#endif

namespace SteamForge {

namespace {
	// The action a freedesktop notification server invokes when the notification itself is clicked.
	constexpr QLatin1StringView DEFAULT_ACTION{"default"};
#ifdef Q_OS_LINUX
	constexpr QLatin1StringView NOTIFICATIONS_SERVICE{"org.freedesktop.Notifications"};
	constexpr QLatin1StringView NOTIFICATIONS_PATH{"/org/freedesktop/Notifications"};
	constexpr QLatin1StringView NOTIFICATIONS_INTERFACE{"org.freedesktop.Notifications"};
	constexpr QLatin1StringView NOTIFY_METHOD{"Notify"};
	constexpr QLatin1StringView ACTION_INVOKED_SIGNAL{"ActionInvoked"};
	constexpr QLatin1StringView NOTIFICATION_CLOSED_SIGNAL{"NotificationClosed"};
	constexpr QLatin1StringView DESKTOP_ENTRY_HINT{"desktop-entry"};
	constexpr uint NEW_NOTIFICATION_ID = 0;
	constexpr int SERVER_DEFAULT_TIMEOUT = -1;
#else
	constexpr std::chrono::milliseconds TRAY_MESSAGE_DURATION{std::chrono::seconds(10)};
#endif
} // namespace

DesktopNotifications::DesktopNotifications(QSystemTrayIcon* windowsTrayIcon, QObject* parent) :
	QObject(parent),
	trayIcon(windowsTrayIcon) {
#ifdef Q_OS_LINUX
	QDBusConnection sessionBus = QDBusConnection::sessionBus();
	sessionBus.connect(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_INTERFACE, ACTION_INVOKED_SIGNAL, this, SLOT(HandleActionInvoked(uint, QString)));
	sessionBus.connect(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_INTERFACE, NOTIFICATION_CLOSED_SIGNAL, this, SLOT(HandleClosed(uint, uint)));
#else
	if (trayIcon != nullptr) {
		connect(trayIcon, &QSystemTrayIcon::messageClicked, this, [this]() {
			if (!lastTrayRecordId.isNull()) {
				Q_EMIT Activated(lastTrayRecordId);
			}
		});
	}
#endif
}

void DesktopNotifications::Show(const QString& title, const QString& text, const QUuid& recordId) {
#ifdef Q_OS_LINUX
	QDBusMessage notifyCall = QDBusMessage::createMethodCall(NOTIFICATIONS_SERVICE, NOTIFICATIONS_PATH, NOTIFICATIONS_INTERFACE, NOTIFY_METHOD);
	notifyCall << QString(APPLICATION_NAME) << NEW_NOTIFICATION_ID << QString(APPLICATION_ID) << title << text;
	notifyCall << QStringList{DEFAULT_ACTION, tr("Open")};
	notifyCall << QVariantMap{{DESKTOP_ENTRY_HINT, QString(APPLICATION_ID)}};
	notifyCall << SERVER_DEFAULT_TIMEOUT;
	auto* callWatcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(notifyCall), this);
	connect(callWatcher, &QDBusPendingCallWatcher::finished, this, [this, recordId](QDBusPendingCallWatcher* finishedWatcher) {
		const QDBusPendingReply<uint> reply = *finishedWatcher;
		if (reply.isValid()) {
			recordIdsByNotification.insert(reply.value(), recordId);
		}
		finishedWatcher->deleteLater();
	});
#else
	if (trayIcon == nullptr || !trayIcon->isVisible()) {
		return;
	}
	// One balloon at a time: a newer notification replaces the older one, and so does its account.
	lastTrayRecordId = recordId;
	trayIcon->showMessage(title, text, QGuiApplication::windowIcon(), static_cast<int>(TRAY_MESSAGE_DURATION.count()));
#endif
}

void DesktopNotifications::HandleActionInvoked(uint notificationId, const QString& actionKey) {
	const auto recordIterator = recordIdsByNotification.constFind(notificationId);
	if (recordIterator == recordIdsByNotification.constEnd() || actionKey != DEFAULT_ACTION) {
		return;
	}
	Q_EMIT Activated(*recordIterator);
}

void DesktopNotifications::HandleClosed(uint notificationId, uint closeReason) {
	Q_UNUSED(closeReason);
	recordIdsByNotification.remove(notificationId);
}

} // namespace SteamForge
