#pragma once

namespace SteamForge {

inline constexpr QLatin1StringView APPLICATION_NAME{"SteamForge"};

// Desktop entry name, Wayland app_id and the prefix of the single-instance endpoint.
inline constexpr QLatin1StringView APPLICATION_ID{"steamforge"};

// Resource prefixes match qt_add_resources and qt_add_translations in CMakeLists.txt.
#ifdef Q_OS_WIN
inline constexpr QLatin1StringView APPLICATION_ICON_RESOURCE{":/icons/app.ico"};
inline constexpr QLatin1StringView TRAY_ICON_RESOURCE{":/icons/tray.ico"};
#else
inline constexpr QLatin1StringView APPLICATION_ICON_RESOURCE{":/icons/app.svg"};
inline constexpr QLatin1StringView TRAY_ICON_RESOURCE{":/icons/tray.svg"};
#endif

inline constexpr QLatin1StringView APPLICATION_TRANSLATION_RESOURCE_PATTERN{":/translations/steamforge_%1.qm"};
inline constexpr QLatin1StringView QT_TRANSLATION_RESOURCE_PATTERN{":/translations/qt/qtbase_%1.qm"};

} // namespace SteamForge
