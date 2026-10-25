#pragma once

namespace SteamForge {

enum class InterfaceLanguage {
	ENGLISH,
	RUSSIAN
};

class InterfaceTranslator final {
public:
	[[nodiscard]] static InterfaceLanguage DetectSystemLanguage();

	[[nodiscard]] bool Apply(InterfaceLanguage interfaceLanguage);

private:
	void Remove();

	QTranslator applicationTranslator;
	QTranslator qtTranslator;
};

} // namespace SteamForge
