#include "app/guard_setup.h"

#include "app/account_library.h"
#include "app/session_access.h"
#include "steam/device_id.h"
#include "steam/guard_code.h"
#include "steam/steam_clock.h"

namespace SteamForge {

namespace {
	GuardSetupFailure ClassifyError(const WebApiError& error) {
		GuardSetupFailureKind kind = GuardSetupFailureKind::STEAM_ERROR;
		if (error.kind == WebApiErrorKind::NETWORK || error.kind == WebApiErrorKind::TIMEOUT) {
			kind = GuardSetupFailureKind::CONNECTION;
		} else if (IsSignInRequired(error)) {
			kind = GuardSetupFailureKind::SIGN_IN_REQUIRED;
		} else if (error.kind == WebApiErrorKind::STEAM_RESULT) {
			switch (error.steamResult) {
				case SteamResult::DUPLICATE_REQUEST:
					kind = GuardSetupFailureKind::ALREADY_PROTECTED;
					break;
				case SteamResult::TWO_FACTOR_ACTIVATION_CODE_MISMATCH:
					kind = GuardSetupFailureKind::WRONG_ACTIVATION_CODE;
					break;
				case SteamResult::TWO_FACTOR_CODE_MISMATCH:
				case SteamResult::TIME_NOT_SYNCED:
					kind = GuardSetupFailureKind::TIME_MISMATCH;
					break;
				default:
					break;
			}
		}
		return {
			.kind = kind,
			.error = error,
			.status = 0,
		};
	}

	GuardSetupFailure AccessFailure(const SessionAccessFailure& accessFailure) {
		switch (accessFailure.kind) {
			case SessionAccessFailureKind::SIGN_IN_REQUIRED:
				return {.kind = GuardSetupFailureKind::SIGN_IN_REQUIRED, .error = accessFailure.error, .status = 0};
			case SessionAccessFailureKind::STORAGE_ERROR:
				return {.kind = GuardSetupFailureKind::STORAGE_ERROR, .error = accessFailure.error, .status = 0};
			case SessionAccessFailureKind::RESPONSE_ERROR:
				break;
		}
		return ClassifyError(accessFailure.error);
	}
} // namespace

GuardSetup::GuardSetup(WebApiTransport& transport, const SteamClock& clock, AccountLibrary& library, AuthenticatorRecord setupRecord, AccountSession setupSession, QObject* parent) :
	QObject(parent),
	authenticationService(transport),
	twoFactorService(transport),
	steamClock(clock),
	accountLibrary(library),
	record(std::move(setupRecord)),
	session(std::move(setupSession)) {
}

void GuardSetup::Start() {
	if (stage != GuardSetupStage::IDLE) {
		return;
	}
	if (record.pendingActivation.has_value()) {
		SetStage(GuardSetupStage::AWAITING_ACTIVATION_CODE);
		return;
	}
	if (!record.secrets.sharedSecret.isEmpty()) {
		return;
	}
	AddAuthenticator();
}

void GuardSetup::SubmitActivationCode(const QString& activationCode) {
	const QString trimmedCode = activationCode.trimmed();
	if (stage != GuardSetupStage::AWAITING_ACTIVATION_CODE || trimmedCode.isEmpty()) {
		return;
	}
	Finalize(trimmedCode);
}

void GuardSetup::Cancel() {
	if (!IsFinished()) {
		SetStage(GuardSetupStage::CANCELLED);
	}
}

GuardSetupStage GuardSetup::Stage() const {
	return stage;
}

const std::optional<GuardSetupFailure>& GuardSetup::Failure() const {
	return failure;
}

const AuthenticatorRecord& GuardSetup::Record() const {
	return record;
}

void GuardSetup::AddAuthenticator() {
	SetStage(GuardSetupStage::ADDING);
	EnsureFreshAccessToken(authenticationService, accountLibrary, session, *this, [this](const std::expected<void, SessionAccessFailure>& access) {
		if (IsFinished()) {
			return;
		}
		if (!access.has_value()) {
			Fail(AccessFailure(access.error()));
			return;
		}
		const QString deviceId = GenerateDeviceId();
		twoFactorService.AddAuthenticator(session.accessToken, session.steamId, deviceId, *this, [this, deviceId](const std::expected<AddedAuthenticator, WebApiError>& added) {
			if (IsFinished()) {
				return;
			}
			if (!added.has_value()) {
				Fail(ClassifyError(added.error()));
				return;
			}
			SavePendingActivation(*added, deviceId);
		});
	});
}

void GuardSetup::SavePendingActivation(const AddedAuthenticator& addedAuthenticator, const QString& deviceId) {
	AuthenticatorRecord pending = record;
	pending.secrets = addedAuthenticator.secrets;
	pending.secrets.deviceId = deviceId;
	if (pending.account.accountName.isEmpty()) {
		pending.account.accountName = addedAuthenticator.accountName;
	}
	pending.pendingActivation = PendingActivation{
		.confirmType = addedAuthenticator.confirmType,
		.phoneNumberHint = addedAuthenticator.phoneNumberHint,
	};
	pending.source = RecordSource{
		.origin = RecordOrigin::ADDED_IN_STEAMFORGE,
		.fileName = {},
		.importedAt = QDateTime::currentDateTimeUtc(),
	};
	// Without the saved secrets the activation code is never sent, so Steam keeps no authenticator SteamForge lost.
	if (const std::expected<void, StorageError> saved = accountLibrary.Save(pending); !saved.has_value()) {
		Fail({.kind = GuardSetupFailureKind::STORAGE_ERROR, .error = {}, .status = 0});
		return;
	}
	record = std::move(pending);
	SetStage(GuardSetupStage::AWAITING_ACTIVATION_CODE);
}

void GuardSetup::Finalize(const QString& activationCode) {
	failure.reset();
	SetStage(GuardSetupStage::FINALIZING);
	EnsureFreshAccessToken(authenticationService, accountLibrary, session, *this, [this, activationCode](const std::expected<void, SessionAccessFailure>& access) {
		if (IsFinished()) {
			return;
		}
		if (!access.has_value()) {
			const GuardSetupFailure accessFailure = AccessFailure(access.error());
			if (accessFailure.kind == GuardSetupFailureKind::SIGN_IN_REQUIRED) {
				Fail(accessFailure);
			} else {
				ReturnToActivationCode(accessFailure);
			}
			return;
		}
		const auto steamTime = std::chrono::floor<std::chrono::seconds>(steamClock.Now());
		const QString authenticatorCode = GenerateGuardCode(record.secrets.sharedSecret, steamTime);
		twoFactorService.FinalizeAddAuthenticator(session.accessToken, session.steamId, authenticatorCode, steamTime, activationCode, *this, [this](const std::expected<FinalizeOutcome, WebApiError>& outcome) {
			if (IsFinished()) {
				return;
			}
			HandleFinalizeResult(outcome);
		});
	});
}

void GuardSetup::HandleFinalizeResult(const std::expected<FinalizeOutcome, WebApiError>& outcome) {
	if (!outcome.has_value()) {
		const GuardSetupFailure finalizeFailure = ClassifyError(outcome.error());
		if (finalizeFailure.kind == GuardSetupFailureKind::SIGN_IN_REQUIRED) {
			Fail(finalizeFailure);
		} else {
			// The record stays pending: after a lost answer the same code may be sent again.
			ReturnToActivationCode(finalizeFailure);
		}
		return;
	}
	if (!outcome->isSuccess) {
		ReturnToActivationCode({.kind = GuardSetupFailureKind::NOT_ACCEPTED, .error = {}, .status = outcome->status});
		return;
	}
	FinishActivation();
}

void GuardSetup::FinishActivation() {
	AuthenticatorRecord active = record;
	active.pendingActivation.reset();
	if (const std::expected<void, StorageError> saved = accountLibrary.Save(active); !saved.has_value()) {
		Fail({.kind = GuardSetupFailureKind::ACTIVATED_NOT_SAVED, .error = {}, .status = 0});
		return;
	}
	record = std::move(active);
	failure.reset();
	SetStage(GuardSetupStage::SUCCEEDED);
}

void GuardSetup::ReturnToActivationCode(GuardSetupFailure problem) {
	failure = std::move(problem);
	SetStage(GuardSetupStage::AWAITING_ACTIVATION_CODE);
}

void GuardSetup::SetStage(GuardSetupStage newStage) {
	stage = newStage;
	Q_EMIT StageChanged();
}

void GuardSetup::Fail(GuardSetupFailure setupFailure) {
	failure = std::move(setupFailure);
	SetStage(GuardSetupStage::FAILED);
}

bool GuardSetup::IsFinished() const {
	return stage == GuardSetupStage::SUCCEEDED || stage == GuardSetupStage::FAILED || stage == GuardSetupStage::CANCELLED;
}

} // namespace SteamForge
