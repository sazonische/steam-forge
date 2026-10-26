#pragma once

#include "domain/account_session.h"
#include "domain/authenticator_record.h"
#include "steam/authentication_service.h"
#include "steam/two_factor_service.h"

namespace SteamForge {

class AccountLibrary;
class SteamClock;

enum class LoginStage {
	IDLE,
	STARTING,
	AWAITING_EMAIL_CODE,
	AWAITING_DEVICE_CODE,
	AWAITING_MOVE_CODE,
	AWAITING_CONFIRMATION,
	FINISHING,
	SUCCEEDED,
	// Steam Guard is now in SteamForge. A session from the sign-in started before the move was rejected by Steam, so a new sign-in follows.
	GUARD_MOVED,
	FAILED,
	CANCELLED
};

enum class LoginFailureKind {
	WRONG_CREDENTIALS,
	RATE_LIMITED,
	WRONG_ACCOUNT,
	TIMED_OUT,
	SESSION_ENDED,
	UNSUPPORTED_CONFIRMATION,
	INVALID_TOKENS,
	ENCRYPTION_FAILED,
	CONNECTION,
	STEAM_ERROR,
	STORAGE_ERROR,
	// Steam moved the authenticator, but its secrets could not be saved; MovedRecord() still holds them.
	GUARD_NOT_SAVED
};

struct LoginFailure {
	LoginFailureKind kind = LoginFailureKind::STEAM_ERROR;
	WebApiError error;
};

// One sign-in attempt as the Steam mobile application, which issues the tokens needed for confirmations
// and QR approval. A record that is not stored yet stands for a new account: it is replaced by a stored
// record of the same SteamID or saved after the sign-in. The account must match the SteamID of the record.
// The successful sign-in is saved before Succeeded is emitted.
class SteamLogin final : public QObject {
	Q_OBJECT

public:
	SteamLogin(WebApiTransport& transport, const SteamClock& clock, AccountLibrary& library, AuthenticatorRecord loginRecord, QObject* parent = nullptr);

	void Start(const QString& accountName, QString password);
	void SubmitCode(const QString& code);
	// Moves Steam Guard of the account to SteamForge with an SMS code, as the Steam mobile application does when
	// the device with the authenticator is unavailable. Called again while waiting for the SMS code, it resends it.
	void StartGuardMove();
	void Cancel();

	[[nodiscard]] LoginStage Stage() const;
	[[nodiscard]] const std::optional<LoginFailure>& Failure() const;
	[[nodiscard]] const QString& EmailDomain() const;
	[[nodiscard]] bool IsCodeRejected() const;
	[[nodiscard]] bool IsAwaitingConfirmationToo() const;
	[[nodiscard]] bool CanMoveGuard() const;
	[[nodiscard]] const std::optional<WebApiError>& MoveError() const;
	[[nodiscard]] const std::optional<AuthenticatorRecord>& MovedRecord() const;
	[[nodiscard]] const AuthenticatorRecord& Record() const;

Q_SIGNALS:
	void StageChanged();
	void Succeeded(const AccountSession& session);
	void GuardMoved();

private:
	void BeginSession(const PasswordRsaKey& key, QString password);
	void HandleSessionStarted(const CredentialsAuthSession& session);
	void AdoptStoredRecord(quint64 steamId);
	void FinishGuardMove(const QString& smsCode);
	void SaveMovedGuard(const MovedAuthenticator& movedAuthenticator);
	void SubmitGeneratedCode();
	void SubmitGuardCode(const QString& code, AuthGuardType codeType, LoginStage stageOnRejection, bool isGeneratedFromRecord);
	void StartPolling();
	void Poll();
	void HandlePollResult(const AuthSessionPollResult& result);
	void FinishWithTokens(const QString& refreshToken, const QString& accessToken);
	void SaveSignIn(const AccountSession& session);
	void SetStage(LoginStage newStage);
	void Fail(LoginFailureKind kind, const WebApiError& error = {});
	void FailWithResponseError(const WebApiError& error);
	[[nodiscard]] bool IsFinished() const;

	AuthenticationService authenticationService;
	TwoFactorService twoFactorService;
	const SteamClock& steamClock;
	AccountLibrary& accountLibrary;
	AuthenticatorRecord record;
	QString enteredAccountName;
	QString signedInAccountName;
	CredentialsAuthSession authSession;
	LoginStage stage = LoginStage::IDLE;
	std::optional<LoginFailure> failure;
	QString emailDomain;
	std::optional<WebApiError> moveError;
	std::optional<AuthenticatorRecord> movedRecord;
	bool isCodeRejected = false;
	// Steam accepted a code generated from the key of the record, which proves whose authenticator it is.
	bool isRecordKeyConfirmed = false;
	bool isPolling = false;
	bool isPollRequestInFlight = false;
	std::chrono::steady_clock::time_point pollDeadline;
	QTimer pollTimer;
};

} // namespace SteamForge
