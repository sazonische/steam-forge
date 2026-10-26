#pragma once

#include "domain/authenticator_record.h"

namespace SteamForge {

class AccountProfiles;
class ElidedLabel;
class PictureView;

class AccountRow final : public QFrame {
	Q_OBJECT

public:
	AccountRow(AuthenticatorRecord accountRecord, AccountProfiles& profiles, QWidget* parent = nullptr);

	[[nodiscard]] const QUuid& RecordId() const;
	void SetSelected(bool isSelected);
	void UpdateCode(std::chrono::sys_seconds steamTime);
	void SetConfirmationCount(int waitingCount);
	// The eye button shows or hides the code and the account name together.
	void SetRevealed(bool isShown);

Q_SIGNALS:
	void SelectionRequested(const QUuid& recordId);
	void NeighbourRequested(int step);
	void RevealToggled(const QUuid& recordId, bool isShown);

protected:
	void mousePressEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void changeEvent(QEvent* event) override;

private:
	void RetranslateUi();
	void ShowCode();
	void ToggleReveal();
	void CopyCode();
	void UpdateProfile();

	AccountProfiles& accountProfiles;
	AuthenticatorRecord record;
	QString personaName;
	QString currentCode;
	std::optional<std::int64_t> currentTimeStep;
	bool isRevealed = false;
	int confirmationCount = 0;
	QTimer copyFeedbackTimer;

	PictureView* avatarPicture = nullptr;
	ElidedLabel* nameLabel = nullptr;
	ElidedLabel* subtitleLabel = nullptr;
	QLabel* confirmationBadge = nullptr;
	QLabel* codeLabel = nullptr;
	QToolButton* revealButton = nullptr;
	QToolButton* copyButton = nullptr;
};

} // namespace SteamForge
