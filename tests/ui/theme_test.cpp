#include "ui/theme.h"

namespace {
constexpr int MINIMUM_DIALOG_WIDTH = 480;
} // namespace

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

	// The texts wrap at the narrower width of the size hint; at the minimum width they need fewer lines, and the
	// lines left over must not stay as gaps between them.
	void FitsADialogToItsWrappedTextsAtItsWidth() {
		auto& application = *qobject_cast<QApplication*>(QCoreApplication::instance());
		SteamForge::ApplyTheme(application);

		QDialog dialog;
		dialog.setMinimumWidth(MINIMUM_DIALOG_WIDTH);
		auto* titleLabel = new QLabel(QStringLiteral("Sign in to the account"), &dialog);
		titleLabel->setWordWrap(true);
		auto* textLabel = new QLabel(QStringLiteral("You won't need a Steam Guard code: SteamForge enters it for you."), &dialog);
		textLabel->setWordWrap(true);
		auto* dialogLayout = new QVBoxLayout(&dialog);
		dialogLayout->addWidget(titleLabel);
		dialogLayout->addWidget(textLabel);
		QVERIFY(dialogLayout->totalSizeHint().width() < MINIMUM_DIALOG_WIDTH);
		dialog.show();
		QVERIFY(QTest::qWaitForWindowExposed(&dialog));

		QCOMPARE(dialog.width(), MINIMUM_DIALOG_WIDTH);
		QCOMPARE(dialog.height(), dialogLayout->totalHeightForWidth(dialog.width()));
	}
};

QTEST_MAIN(ThemeTest)

#include "theme_test.moc"
