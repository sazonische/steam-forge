#pragma once

#include "domain/account_session.h"
#include "domain/authenticator_record.h"
#include "steam/authentication_service.h"
#include "steam/qr_challenge.h"

namespace SteamForge {

class AccountLibrary;

enum class LoginApprovalStage {
	IDLE,
	SEARCHING,
	LOADING_DETAILS,
	AWAITING_DECISION,
	SENDING,
	APPROVED,
	DENIED,
	NOTHING_PENDING,
	FAILED,
	CANCELLED
};

enum class LoginApprovalFailureKind {
	NO_QR_FOUND,
	SEVERAL_QR_FOUND,
	UNSUPPORTED_VERSION,
	MISSING_CODE_KEY,
	SIGN_IN_REQUIRED,
	REQUEST_ENDED,
	CONNECTION,
	STEAM_ERROR,
	STORAGE_ERROR
};

struct LoginApprovalFailure {
	LoginApprovalFailureKind kind = LoginApprovalFailureKind::STEAM_ERROR;
	WebApiError error;
};

// Approves or denies one Steam sign-in request of the record's account, as the Steam mobile application does:
// a request shown as a QR code, or one waiting for approval after a sign-in with the password.
class LoginApproval final : public QObject {
	Q_OBJECT

public:
	LoginApproval(WebApiTransport& transport, AccountLibrary& library, AuthenticatorRecord approvingRecord, AccountSession approvingSession, QObject* parent = nullptr);

	void ProcessScreenImages(const QList<QImage>& images);
	void CheckPendingRequests();
	// Whether the device that asks stays signed in is the approver's choice, as in the mobile application.
	void Approve(bool isSignInRemembered);
	void Deny();
	void Cancel();

	[[nodiscard]] LoginApprovalStage Stage() const;
	[[nodiscard]] const std::optional<LoginApprovalFailure>& Failure() const;
	[[nodiscard]] const AuthSessionInfo& SessionInfo() const;
	[[nodiscard]] std::size_t OtherPendingRequestCount() const;
	// A request waiting after a sign-in with the password rather than one from a QR code.
	[[nodiscard]] bool IsPasswordSignIn() const;

Q_SIGNALS:
	void StageChanged();

private:
	[[nodiscard]] bool BeginRequest();
	void WithAccessToken(std::function<void()> continuation);
	void LoadDetails(quint64 clientId, std::optional<int> knownVersion);
	void SendDecision(bool isApproved, bool isSignInRemembered);
	void SetStage(LoginApprovalStage newStage);
	void Fail(LoginApprovalFailureKind kind, const WebApiError& error = {});
	void FailWithResponseError(const WebApiError& error);
	[[nodiscard]] bool IsFinished() const;

	AuthenticationService authenticationService;
	AccountLibrary& accountLibrary;
	AuthenticatorRecord record;
	AccountSession session;
	QrChallenge request;
	AuthSessionInfo sessionInfo;
	std::size_t otherPendingRequestCount = 0;
	bool isPasswordSignIn = false;
	LoginApprovalStage stage = LoginApprovalStage::IDLE;
	std::optional<LoginApprovalFailure> failure;
};

} // namespace SteamForge
