#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr const char* BUTTON_ROLE_PROPERTY = "buttonRole";
	constexpr const char* EXPIRING_PROPERTY = "expiring";
	constexpr const char* SELECTED_PROPERTY = "selected";
	constexpr const char* ATTENTION_PROPERTY = "attention";
	constexpr const char* KEYBOARD_FOCUS_PROPERTY = "keyboardFocus";
	constexpr const char* DROP_ACTIVE_PROPERTY = "dropActive";
	constexpr const char* COMPACT_PROPERTY = "compact";
	constexpr QLatin1StringView SECONDARY_BUTTON_ROLE{"secondary"};
	constexpr QLatin1StringView PRIMARY_BUTTON_ROLE{"primary"};
	constexpr qreal SECTION_LABEL_LETTER_SPACING_PERCENT = 102;

	QLatin1StringView ButtonRoleName(ButtonRole buttonRole) {
		switch (buttonRole) {
			case ButtonRole::SECONDARY:
				return SECONDARY_BUTTON_ROLE;
			case ButtonRole::PRIMARY:
				return PRIMARY_BUTTON_ROLE;
		}
		return SECONDARY_BUTTON_ROLE;
	}

	const char* StyleFlagProperty(StyleFlag styleFlag) {
		switch (styleFlag) {
			case StyleFlag::EXPIRING:
				return EXPIRING_PROPERTY;
			case StyleFlag::SELECTED:
				return SELECTED_PROPERTY;
			case StyleFlag::ATTENTION:
				return ATTENTION_PROPERTY;
			case StyleFlag::KEYBOARD_FOCUS:
				return KEYBOARD_FOCUS_PROPERTY;
			case StyleFlag::DROP_ACTIVE:
				return DROP_ACTIVE_PROPERTY;
		}
		return EXPIRING_PROPERTY;
	}

} // namespace

void SetButtonRole(QPushButton& button, ButtonRole buttonRole) {
	button.setProperty(BUTTON_ROLE_PROPERTY, QString(ButtonRoleName(buttonRole)));
	Repolish(button);
}

void MakeCompact(QPushButton& button) {
	button.setProperty(COMPACT_PROPERTY, true);
	Repolish(button);
}

void FitHeightToContents(QWidget& window) {
	QLayout* layout = window.layout();
	// Before the first show Qt sizes the window from its layout itself, wrapped texts included.
	if (layout == nullptr || !window.isVisible()) {
		return;
	}
	layout->activate();
	const int contentHeight = layout->hasHeightForWidth() ? layout->totalHeightForWidth(window.width()) : layout->totalSizeHint().height();
	window.resize(window.width(), std::max(contentHeight, layout->totalMinimumSize().height()));
}

void SetStyleFlag(QWidget& widget, StyleFlag styleFlag, bool isSet) {
	const char* propertyName = StyleFlagProperty(styleFlag);
	if (widget.property(propertyName).toBool() == isSet) {
		return;
	}
	widget.setProperty(propertyName, isSet);
	Repolish(widget);
}

void SetStyleName(QWidget& widget, QLatin1StringView styleName) {
	if (widget.objectName() == styleName) {
		return;
	}
	widget.setObjectName(styleName);
	Repolish(widget);
}

// Style sheet rules with property selectors are evaluated at polish time only.
void Repolish(QWidget& widget) {
	widget.style()->unpolish(&widget);
	widget.style()->polish(&widget);
	widget.update();
}

QFrame* CreateRowDivider(QWidget* parent) {
	auto* divider = new QFrame(parent);
	divider->setObjectName(ROW_DIVIDER_STYLE);
	return divider;
}

QLabel* CreateLabel(QWidget* parent, QLatin1StringView styleName) {
	auto* label = new QLabel(parent);
	if (!styleName.isEmpty()) {
		label->setObjectName(styleName);
	}
	return label;
}

QLabel* CreatePlainLabel(QWidget* parent, QLatin1StringView styleName) {
	QLabel* label = CreateLabel(parent, styleName);
	label->setTextFormat(Qt::PlainText);
	label->setWordWrap(true);
	return label;
}

void MakeSectionLabel(QLabel& label) {
	label.setObjectName(SECTION_LABEL_STYLE);
	QFont font = label.font();
	font.setCapitalization(QFont::AllUppercase);
	font.setLetterSpacing(QFont::PercentageSpacing, SECTION_LABEL_LETTER_SPACING_PERCENT);
	label.setFont(font);
}

} // namespace SteamForge
