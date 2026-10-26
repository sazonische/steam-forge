#pragma once

#include "app/login_approval.h"
#include "platform/screen_capture.h"
#include "steam/web_api_transport.h"

namespace SteamForge {

class AccountLibrary;
class IllustrationView;

class LoginApprovalDialog final : public QDialog {
	Q_OBJECT

public:
	LoginApprovalDialog(AccountLibrary& library, AuthenticatorRecord approvingRecord, QString title, AccountSession approvingSession, QWidget* parent = nullptr);

	void reject() override;

Q_SIGNALS:
	// The dialog has closed; the record needs a new sign-in before approvals work again.
	void SignInAgainRequested(const QUuid& recordId);

protected:
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	void RetranslateUi();
	void CheckPendingRequests();
	void FindQrCode();
	void CaptureScreens();
	void RestoreWindows();
	void HandleCaptured(const QList<QImage>& images);
	void HandleCaptureFailure(ScreenCaptureFailure failureReason);
	void StartApproval();
	void UpdateState();
	[[nodiscard]] QString StatusText() const;
	void UpdateDetails();
	[[nodiscard]] static QString FailureText(const LoginApprovalFailure& approvalFailure);

	AccountLibrary& accountLibrary;
	AuthenticatorRecord record;
	// The Steam name, or the account name while Steam has told none.
	QString accountTitle;
	AccountSession session;
	SteamWebApiTransport transport;
	ScreenCapture screenCapture;
	std::unique_ptr<LoginApproval> approval;
	std::optional<ScreenCaptureFailure> captureFailure;
	bool isCapturing = false;
	bool wasMainWindowVisible = false;
	bool hasCheckedOnOpen = false;

	IllustrationView* illustration = nullptr;
	QLabel* introLabel = nullptr;
	QLabel* statusLabel = nullptr;
	QFrame* detailsCard = nullptr;
	QLabel* accountTitleLabel = nullptr;
	QLabel* accountValueLabel = nullptr;
	QLabel* deviceTitleLabel = nullptr;
	QLabel* deviceValueLabel = nullptr;
	QLabel* locationTitleLabel = nullptr;
	QLabel* locationValueLabel = nullptr;
	QLabel* ipTitleLabel = nullptr;
	QLabel* ipValueLabel = nullptr;
	QLabel* warningsLabel = nullptr;
	QCheckBox* rememberSignInCheckBox = nullptr;
	QPushButton* checkButton = nullptr;
	QPushButton* findQrButton = nullptr;
	QPushButton* approveButton = nullptr;
	QPushButton* denyButton = nullptr;
	QPushButton* signInAgainButton = nullptr;
	QPushButton* changePasswordButton = nullptr;
	QPushButton* closeButton = nullptr;
};

} // namespace SteamForge
