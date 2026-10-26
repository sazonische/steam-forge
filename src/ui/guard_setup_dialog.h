#pragma once

#include "app/guard_setup.h"
#include "steam/web_api_transport.h"

namespace SteamForge {

class AccountLibrary;
class IllustrationView;
class SteamClock;

class GuardSetupDialog final : public QDialog {
	Q_OBJECT

public:
	GuardSetupDialog(AccountLibrary& library, const SteamClock& clock, AuthenticatorRecord setupRecord, QString title, AccountSession setupSession, QWidget* parent = nullptr);

	void reject() override;

protected:
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	void RetranslateUi();
	void StartSetup();
	void SubmitCode();
	void UpdateState();
	[[nodiscard]] QString IntroText() const;
	[[nodiscard]] QString CodePrompt() const;
	[[nodiscard]] QString StatusText() const;
	[[nodiscard]] static QString FailureText(const GuardSetupFailure& setupFailure);

	AuthenticatorRecord record;
	// The Steam name, or the account name while Steam has told none.
	QString accountTitle;
	SteamWebApiTransport transport;
	GuardSetup setup;
	bool hasResumedOnOpen = false;

	IllustrationView* illustration = nullptr;
	QLabel* headlineLabel = nullptr;
	QLabel* introLabel = nullptr;
	QWidget* codeRow = nullptr;
	QLabel* codeLabel = nullptr;
	QLineEdit* codeEdit = nullptr;
	QPushButton* confirmCodeButton = nullptr;
	QFrame* recoveryCard = nullptr;
	QLabel* recoveryTitleLabel = nullptr;
	QLabel* recoveryCodeLabel = nullptr;
	QLabel* recoveryHintLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QPushButton* startButton = nullptr;
	QPushButton* closeButton = nullptr;
};

} // namespace SteamForge
