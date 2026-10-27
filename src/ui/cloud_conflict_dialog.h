#pragma once

#include "app/cloud_sync.h"

namespace SteamForge {

class SteamClock;

// Shows the version on this computer and the one in Google side by side with their current codes: for two different
// Steam Guards of an account, the code that Steam accepts tells which one works.
class CloudConflictDialog final : public QDialog {
	Q_OBJECT

public:
	CloudConflictDialog(const CloudConflict& conflict, const AuthenticatorRecord& localRecord, const QString& accountTitle, const SteamClock& clock, QWidget* parent = nullptr);

	[[nodiscard]] std::optional<ConflictChoice> Choice() const;
	// The conflict the choice is for.
	[[nodiscard]] const QString& CloudRecordId() const;

private:
	[[nodiscard]] QFrame* CreateVersionCard(const QString& title, const AuthenticatorRecord& record, QLabel*& codeLabel);
	void UpdateCodes();

	const SteamClock& steamClock;
	QString cloudRecordId;
	QByteArray localSharedSecret;
	QByteArray cloudSharedSecret;
	QLabel* localCodeLabel = nullptr;
	QLabel* cloudCodeLabel = nullptr;
	QButtonGroup choiceGroup;
	QPushButton* applyButton = nullptr;
	QTimer codeTimer;
};

} // namespace SteamForge
