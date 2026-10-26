#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr const char* BUTTON_ROLE_PROPERTY = "buttonRole";
	constexpr const char* EXPIRING_PROPERTY = "expiring";
	constexpr const char* SELECTED_PROPERTY = "selected";
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
		}
		return EXPIRING_PROPERTY;
	}

	// Style sheet rules with property selectors are evaluated at polish time only.
	void Repolish(QWidget& widget) {
		widget.style()->unpolish(&widget);
		widget.style()->polish(&widget);
		widget.update();
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

void MakeSectionLabel(QLabel& label) {
	label.setObjectName(SECTION_LABEL_STYLE);
	QFont font = label.font();
	font.setCapitalization(QFont::AllUppercase);
	font.setLetterSpacing(QFont::PercentageSpacing, SECTION_LABEL_LETTER_SPACING_PERCENT);
	label.setFont(font);
}

} // namespace SteamForge
