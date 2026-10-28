#pragma once

namespace SteamForge {

// Object names that resources/styles/steamforge.qss selects.
inline constexpr QLatin1StringView PAGE_TITLE_STYLE{"PageTitle"};
inline constexpr QLatin1StringView EMPTY_STATE_TITLE_STYLE{"EmptyStateTitle"};
inline constexpr QLatin1StringView EMPTY_STATE_TEXT_STYLE{"EmptyStateText"};
inline constexpr QLatin1StringView SECONDARY_TEXT_STYLE{"SecondaryText"};
inline constexpr QLatin1StringView SECTION_LABEL_STYLE{"SectionLabel"};
inline constexpr QLatin1StringView CAPABILITY_AVAILABLE_STYLE{"CapabilityAvailable"};
inline constexpr QLatin1StringView CAPABILITY_MISSING_STYLE{"CapabilityMissing"};
inline constexpr QLatin1StringView ERROR_TEXT_STYLE{"ErrorText"};
inline constexpr QLatin1StringView NOTICE_STYLE{"Notice"};
inline constexpr QLatin1StringView PROBLEM_BANNER_STYLE{"ProblemBanner"};
inline constexpr QLatin1StringView TOP_BAR_STYLE{"TopBar"};
inline constexpr QLatin1StringView TOP_BAR_TITLE_STYLE{"TopBarTitle"};
inline constexpr QLatin1StringView CARD_STYLE{"Card"};
inline constexpr QLatin1StringView ACCOUNT_LIST_STYLE{"AccountList"};
inline constexpr QLatin1StringView ACCOUNT_SCROLL_STYLE{"AccountScroll"};
inline constexpr QLatin1StringView ACCOUNT_ROWS_STYLE{"AccountRows"};
inline constexpr QLatin1StringView DETAILS_SCROLL_STYLE{"DetailsScroll"};
inline constexpr QLatin1StringView DETAILS_PAGE_STYLE{"DetailsPage"};
inline constexpr QLatin1StringView ICON_BUTTON_STYLE{"IconButton"};
inline constexpr QLatin1StringView ACCOUNT_ROW_STYLE{"AccountRow"};
inline constexpr QLatin1StringView ROW_NAME_STYLE{"RowName"};
inline constexpr QLatin1StringView ROW_SUBTITLE_STYLE{"RowSubtitle"};
inline constexpr QLatin1StringView ROW_CODE_STYLE{"RowCode"};
inline constexpr QLatin1StringView ROW_BADGE_STYLE{"RowBadge"};
inline constexpr QLatin1StringView ROW_ICON_BUTTON_STYLE{"RowIconButton"};
inline constexpr QLatin1StringView CODE_COUNTDOWN_STYLE{"CodeCountdown"};
inline constexpr QLatin1StringView CONFIRMATION_CARD_STYLE{"ConfirmationCard"};
inline constexpr QLatin1StringView FILTER_TAB_STYLE{"FilterTab"};
inline constexpr QLatin1StringView DROP_ZONE_STYLE{"DropZone"};
inline constexpr QLatin1StringView LINK_BUTTON_STYLE{"LinkButton"};
inline constexpr QLatin1StringView STATUS_CHIP_STYLE{"StatusChip"};
inline constexpr QLatin1StringView QR_REVEAL_BUTTON_STYLE{"QrRevealButton"};
// The main line of a state in a card, such as "Signed in"; the details go below in secondary text.
inline constexpr QLatin1StringView STATE_HEADLINE_STYLE{"StateHeadline"};
// Quiet text a little larger than the hints, such as the lines under the account name and under a state.
inline constexpr QLatin1StringView DETAIL_TEXT_STYLE{"DetailText"};
// A saved secret shown as dots.
inline constexpr QLatin1StringView MASKED_SECRET_STYLE{"MaskedSecret"};
// A colored dot before a state: done, waiting for the user, or not started.
inline constexpr QLatin1StringView STATE_DOT_DONE_STYLE{"StateDotDone"};
inline constexpr QLatin1StringView STATE_DOT_ATTENTION_STYLE{"StateDotAttention"};
inline constexpr QLatin1StringView STATE_DOT_IDLE_STYLE{"StateDotIdle"};
inline constexpr QLatin1StringView ROW_DIVIDER_STYLE{"RowDivider"};
// A framed list of rows with dividers, such as the accounts in Google or the answers to a question.
inline constexpr QLatin1StringView LIST_FRAME_STYLE{"ListFrame"};
inline constexpr QLatin1StringView SETTINGS_NAVIGATION_STYLE{"SettingsNavigation"};

// Boolean states that style sheet rules select with property selectors.
enum class StyleFlag {
	EXPIRING,
	SELECTED,
	// A status that waits for the user, such as a locked SteamForge.
	ATTENTION,
	// The focus came from the keyboard; a click or a dialog that just opened shows no focus frame.
	KEYBOARD_FOCUS,
	// Files are dragged over the drop zone.
	DROP_ACTIVE
};

enum class ButtonRole {
	SECONDARY,
	PRIMARY
};

void SetButtonRole(QPushButton& button, ButtonRole buttonRole);

void MakeCompact(QPushButton& button);
// Sets the height a shown window needs for its wrapped texts at its current width; the minimum size
// of a layout ignores wrapping, so a window whose rows change would otherwise cut lines.
void FitHeightToContents(QWidget& window);

void SetStyleFlag(QWidget& widget, StyleFlag styleFlag, bool isSet);
// Changes the style name that the style sheet selects and applies it at once.
void SetStyleName(QWidget& widget, QLatin1StringView styleName);
// Applies the style sheet again after a property it selects on has changed.
void Repolish(QWidget& widget);
// The thin line between the rows of a list.
[[nodiscard]] QFrame* CreateRowDivider(QWidget* parent);
[[nodiscard]] QLabel* CreateLabel(QWidget* parent, QLatin1StringView styleName = {});
// A wrapping label for texts from Steam or from the user, which are never interpreted as rich text.
[[nodiscard]] QLabel* CreatePlainLabel(QWidget* parent, QLatin1StringView styleName = {});

// Uppercase through the font, so translations stay in normal case.
void MakeSectionLabel(QLabel& label);

} // namespace SteamForge
