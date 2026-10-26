#include "app/application_settings.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView LANGUAGE_KEY{"interface/language"};
	constexpr QLatin1StringView MAIN_WINDOW_GEOMETRY_KEY{"window/mainGeometry"};
	constexpr QLatin1StringView UPDATE_CHECKS_KEY{"updates/check"};
	constexpr QLatin1StringView SKIPPED_UPDATE_VERSION_KEY{"updates/skippedVersion"};
	constexpr QLatin1StringView SYSTEM_LANGUAGE_VALUE{"system"};
	constexpr QLatin1StringView ENGLISH_LANGUAGE_VALUE{"en"};
	constexpr QLatin1StringView RUSSIAN_LANGUAGE_VALUE{"ru"};

	QLatin1StringView LanguageValue(std::optional<InterfaceLanguage> language) {
		if (!language.has_value()) {
			return SYSTEM_LANGUAGE_VALUE;
		}
		switch (*language) {
			case InterfaceLanguage::ENGLISH:
				return ENGLISH_LANGUAGE_VALUE;
			case InterfaceLanguage::RUSSIAN:
				return RUSSIAN_LANGUAGE_VALUE;
		}
		return SYSTEM_LANGUAGE_VALUE;
	}
} // namespace

ApplicationSettings::ApplicationSettings(const QString& filePath) :
	settings(filePath, QSettings::IniFormat) {
}

std::optional<InterfaceLanguage> ApplicationSettings::Language() const {
	const QString value = settings.value(LANGUAGE_KEY).toString();
	if (value == ENGLISH_LANGUAGE_VALUE) {
		return InterfaceLanguage::ENGLISH;
	}
	if (value == RUSSIAN_LANGUAGE_VALUE) {
		return InterfaceLanguage::RUSSIAN;
	}
	return std::nullopt;
}

void ApplicationSettings::SetLanguage(std::optional<InterfaceLanguage> language) {
	settings.setValue(LANGUAGE_KEY, QString(LanguageValue(language)));
}

QByteArray ApplicationSettings::MainWindowGeometry() const {
	return settings.value(MAIN_WINDOW_GEOMETRY_KEY).toByteArray();
}

void ApplicationSettings::SetMainWindowGeometry(const QByteArray& geometry) {
	settings.setValue(MAIN_WINDOW_GEOMETRY_KEY, geometry);
}

bool ApplicationSettings::ChecksForUpdates() const {
	return settings.value(UPDATE_CHECKS_KEY, true).toBool();
}

void ApplicationSettings::SetChecksForUpdates(bool isEnabled) {
	settings.setValue(UPDATE_CHECKS_KEY, isEnabled);
}

QVersionNumber ApplicationSettings::SkippedUpdateVersion() const {
	return QVersionNumber::fromString(settings.value(SKIPPED_UPDATE_VERSION_KEY).toString());
}

void ApplicationSettings::SetSkippedUpdateVersion(const QVersionNumber& version) {
	settings.setValue(SKIPPED_UPDATE_VERSION_KEY, version.toString());
}

} // namespace SteamForge
