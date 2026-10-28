#pragma once

namespace SteamForge {

inline constexpr int LIST_AVATAR_EDGE = 28;
inline constexpr QMargins LIST_ROW_MARGINS{16, 8, 16, 8};
inline constexpr int LIST_ROW_SPACING = 12;

// The frame of a list, as in the Steam settings; its layout puts the parts one under another without gaps.
[[nodiscard]] QFrame* CreateListFrame(QWidget* parent);

// Rows one under another with a thin line between them.
class RowList final : public QWidget {
public:
	explicit RowList(QWidget* parent = nullptr);

	void AddRow(QWidget* row);
	// A row may still be on the stack of its own handler, so the rows are deleted after it returns.
	void Clear();

private:
	QVBoxLayout* rowsLayout = nullptr;
};

// Shows a few rows whole and scrolls a long list.
class ScrollingRowList final : public QScrollArea {
public:
	ScrollingRowList(int maximumListHeight, QWidget* parent = nullptr);

	void AddRow(QWidget* row);
	void Clear();

private:
	void FitHeight();

	RowList* rowList = nullptr;
	int maximumHeight = 0;
};

// The parts of an account row that its list fills in: the tick, and the layout for a state or a button at the end.
struct AccountChoiceRow {
	QWidget* row = nullptr;
	QCheckBox* checkBox = nullptr;
	QHBoxLayout* layout = nullptr;
};

// The tick, the picture and the name of an account. A hidden tick keeps its place, so the names stay in one column.
[[nodiscard]] AccountChoiceRow CreateAccountChoiceRow(QWidget* picture, const QString& title, QWidget* parent);

} // namespace SteamForge
