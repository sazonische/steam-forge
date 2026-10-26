#pragma once

#include "domain/account_session.h"
#include "domain/authenticator_record.h"
#include "steam/authentication_service.h"
#include "steam/two_factor_service.h"

namespace SteamForge {

class AccountLibrary;
class SteamClock;

enum class GuardSetupStage {
	IDLE,
	ADDING,
	AWAITING_ACTIVATION_CODE,
	FINALIZING,
	SUCCEEDED,
	FAILED,
	CANCELLED
};

enum class GuardSetupFailureKind {
	SIGN_IN_REQUIRED,
	ALREADY_PROTECTED,
	WRONG_ACTIVATION_CODE,
	TIME_MISMATCH,
	NOT_ACCEPTED,
	CONNECTION,
	STEAM_ERROR,
	STORAGE_ERROR,
	// Steam activated the authenticator, but the record still says the activation is pending.
	ACTIVATED_NOT_SAVED
};

struct GuardSetupFailure {
	GuardSetupFailureKind kind = GuardSetupFailureKind::STEAM_ERROR;
	WebApiError error;
	// Status Steam reports in the body when it answers without success.
	int status = 0;
};

// Makes SteamForge the Steam Guard mobile authenticator of a signed-in account. The new secrets are
// saved as a pending activation before the activation code is sent, so an interrupted activation never
// leaves Steam with an authenticator whose secrets are lost. A pending record resumes at the code.
class GuardSetup final : public QObject {
	Q_OBJECT

public:
	GuardSetup(WebApiTransport& transport, const SteamClock& clock, AccountLibrary& library, AuthenticatorRecord setupRecord, AccountSession setupSession, QObject* parent = nullptr);

	void Start();
	void SubmitActivationCode(const QString& activationCode);
	void Cancel();

	[[nodiscard]] GuardSetupStage Stage() const;
	// The reason of FAILED, or the last problem while the activation code is awaited again.
	[[nodiscard]] const std::optional<GuardSetupFailure>& Failure() const;
	[[nodiscard]] const AuthenticatorRecord& Record() const;

Q_SIGNALS:
	void StageChanged();

private:
	void AddAuthenticator();
	void SavePendingActivation(const AddedAuthenticator& addedAuthenticator, const QString& deviceId);
	void Finalize(const QString& activationCode);
	void HandleFinalizeResult(const std::expected<FinalizeOutcome, WebApiError>& outcome);
	void FinishActivation();
	void ReturnToActivationCode(GuardSetupFailure problem);
	void SetStage(GuardSetupStage newStage);
	void Fail(GuardSetupFailure setupFailure);
	[[nodiscard]] bool IsFinished() const;

	AuthenticationService authenticationService;
	TwoFactorService twoFactorService;
	const SteamClock& steamClock;
	AccountLibrary& accountLibrary;
	AuthenticatorRecord record;
	AccountSession session;
	GuardSetupStage stage = GuardSetupStage::IDLE;
	std::optional<GuardSetupFailure> failure;
};

} // namespace SteamForge
