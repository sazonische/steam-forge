#include "app/interface_translator.h"

class InterfaceTranslatorTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void RussianTranslatesApplicationAndQtStrings() {
		SteamForge::InterfaceTranslator interfaceTranslator;
		QVERIFY(interfaceTranslator.Apply(SteamForge::InterfaceLanguage::RUSSIAN));

		QCOMPARE(QCoreApplication::translate("SteamForge::MainWindow", "No accounts yet"), QStringLiteral("Аккаунтов пока нет"));
		QVERIFY(QCoreApplication::translate("QPlatformTheme", "Cancel") != QStringLiteral("Cancel"));
	}

	void EnglishRestoresSourceStrings() {
		SteamForge::InterfaceTranslator interfaceTranslator;
		QVERIFY(interfaceTranslator.Apply(SteamForge::InterfaceLanguage::RUSSIAN));
		QVERIFY(interfaceTranslator.Apply(SteamForge::InterfaceLanguage::ENGLISH));

		QCOMPARE(QCoreApplication::translate("SteamForge::MainWindow", "No accounts yet"), QStringLiteral("No accounts yet"));
	}

	void DestroyedTranslatorLeavesNoTranslation() {
		{
			SteamForge::InterfaceTranslator interfaceTranslator;
			QVERIFY(interfaceTranslator.Apply(SteamForge::InterfaceLanguage::RUSSIAN));
		}
		QCOMPARE(QCoreApplication::translate("SteamForge::MainWindow", "No accounts yet"), QStringLiteral("No accounts yet"));
	}
};

QTEST_MAIN(InterfaceTranslatorTest)

#include "interface_translator_test.moc"
