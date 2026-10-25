#include "app/interface_translator.h"

#include "common/constants.h"

namespace SteamForge {

namespace {
	QString TranslationLocaleSuffix(InterfaceLanguage interfaceLanguage) {
		switch (interfaceLanguage) {
			case InterfaceLanguage::ENGLISH:
				return {};
			case InterfaceLanguage::RUSSIAN:
				return QStringLiteral("ru");
		}
		return {};
	}
} // namespace

InterfaceLanguage InterfaceTranslator::DetectSystemLanguage() {
	const QStringList preferredLanguageTags = QLocale::system().uiLanguages();
	for (const QString& languageTag : preferredLanguageTags) {
		const QLocale::Language language = QLocale(languageTag).language();
		if (language == QLocale::Russian) {
			return InterfaceLanguage::RUSSIAN;
		}
		if (language == QLocale::English) {
			return InterfaceLanguage::ENGLISH;
		}
	}
	return InterfaceLanguage::ENGLISH;
}

bool InterfaceTranslator::Apply(InterfaceLanguage interfaceLanguage) {
	Remove();

	// English is the source language of the interface strings.
	const QString localeSuffix = TranslationLocaleSuffix(interfaceLanguage);
	if (localeSuffix.isEmpty()) {
		return true;
	}

	const bool applicationTranslationLoaded = applicationTranslator.load(APPLICATION_TRANSLATION_RESOURCE_PATTERN.arg(localeSuffix));
	const bool qtTranslationLoaded = qtTranslator.load(QT_TRANSLATION_RESOURCE_PATTERN.arg(localeSuffix));
	if (!applicationTranslationLoaded || !qtTranslationLoaded) {
		return false;
	}
	return QCoreApplication::installTranslator(&applicationTranslator) && QCoreApplication::installTranslator(&qtTranslator);
}

void InterfaceTranslator::Remove() {
	QCoreApplication::removeTranslator(&qtTranslator);
	QCoreApplication::removeTranslator(&applicationTranslator);
}

} // namespace SteamForge
