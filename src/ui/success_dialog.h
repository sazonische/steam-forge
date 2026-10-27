#pragma once

namespace SteamForge {

class IllustrationView;

// A short moment of success: what is done and what comes next, with a value to keep if there is one.
class SuccessDialog final : public QDialog {
	Q_OBJECT

public:
	SuccessDialog(const QString& headline, const QString& text, QWidget* parent = nullptr);

	// Shown large and selectable, such as a new recovery code.
	void SetKeptValue(const QString& title, const QString& value, const QString& hint);
	// A box to tick below the text, such as adding the account to Google.
	void SetChoice(const QString& text, bool isChecked);
	[[nodiscard]] bool IsChoiceChecked() const;

private:
	IllustrationView* illustration = nullptr;
	QLabel* headlineLabel = nullptr;
	QLabel* textLabel = nullptr;
	QFrame* keptValueCard = nullptr;
	QLabel* keptValueTitleLabel = nullptr;
	QLabel* keptValueLabel = nullptr;
	QLabel* keptValueHintLabel = nullptr;
	QCheckBox* choiceCheckBox = nullptr;
	QPushButton* doneButton = nullptr;
};

} // namespace SteamForge
