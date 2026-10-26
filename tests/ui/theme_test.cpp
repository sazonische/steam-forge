#include "ui/theme.h"

class ThemeTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void FramesOnlyKeyboardFocus() {
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);
		// Applying the theme again keeps a single marker.
		SteamForge::ApplyTheme(application);

		QWidget window;
		auto* firstButton = new QPushButton(QStringLiteral("First"), &window);
		auto* secondButton = new QPushButton(QStringLiteral("Second"), &window);
		auto* layout = new QVBoxLayout(&window);
		layout->addWidget(firstButton);
		layout->addWidget(secondButton);
		window.show();
		window.activateWindow();
		QVERIFY(QTest::qWaitForWindowActive(&window));

		firstButton->setFocus(Qt::MouseFocusReason);
		QVERIFY(!firstButton->property("keyboardFocus").toBool());
		secondButton->setFocus(Qt::TabFocusReason);
		QVERIFY(secondButton->property("keyboardFocus").toBool());
		firstButton->setFocus(Qt::OtherFocusReason);
		QVERIFY(!secondButton->property("keyboardFocus").toBool());
		QVERIFY(!firstButton->property("keyboardFocus").toBool());
	}
};

QTEST_MAIN(ThemeTest)

#include "theme_test.moc"
