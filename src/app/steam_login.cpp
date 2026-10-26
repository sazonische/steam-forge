#include "app/steam_login.h"

#include "app/account_library.h"
#include "crypto/rsa_encryption.h"
#include "steam/device_id.h"
#include "steam/guard_code.h"
#include "steam/jwt_claims.h"
#include "steam/steam_clock.h"

namespace SteamForge {

namespace {
	// Confirmation in the mobile application or by email may take the user a while.
	constexpr std::chrono::minutes POLL_TIMEOUT{3};
	constexpr std::chrono::milliseconds SHORTEST_POLL_INTERVAL{std::chrono::seconds(1)};
	constexpr QLatin1StringView MOBILE_TOKEN_AUDIENCE{"mobile"};

	bool Allows(const CredentialsAuthSession& session, AuthGuardType guardType) {
		return std::ranges::any_of(session.allowedConfirmations, [guardType](const AllowedConfirmation& confirmation) {
			return confirmation.type == guardType;
		});
	}

	bool IsCodeRejection(const WebApiError& error) {
		return error.kind == WebApiErrorKind::STEAM_RESULT && (error.steamResult == SteamResult::TWO_FACTOR_CODE_MISMATCH || error.steamResult == SteamResult::INVALID_LOGIN_AUTH_CODE);
	}

	LoginFailureKind ClassifyError(const WebApiError& error) {
		if (error.kind == WebApiErrorKind::NETWORK || error.kind == WebApiErrorKind::TIMEOUT) {
			return LoginFailureKind::CONNECTION;
		}
		if (error.kind != WebApiErrorKind::STEAM_RESULT) {
			return LoginFailureKind::STEAM_ERROR;
		}
		switch (error.steamResult) {
			case SteamResult::INVALID_PASSWORD:
				return LoginFailureKind::WRONG_CREDENTIALS;
			case SteamResult::RATE_LIMIT_EXCEEDED:
			case SteamResult::ACCOUNT_LOGIN_DENIED_THROTTLE:
				return LoginFailureKind::RATE_LIMITED;
			case SteamResult::FILE_NOT_FOUND:
			case SteamResult::EXPIRED:
				return LoginFailureKind::SESSION_ENDED;
			default:
				return LoginFailureKind::STEAM_ERROR;
		}
	}

	void Wipe(QString& text) {
		text.fill(QChar(u'\0'));
		text.clear();
	}

	void Wipe(QByteArray& bytes) {
		bytes.fill('\0');
		bytes.clear();
	}
} // namespace

SteamLogin::SteamLogin(WebApiTransport& transport, const SteamClock& clock, AccountLibrary& library, AuthenticatorRecord loginRecord, QObject* parent) :
	QObject(parent),
	authenticationService(transport),
	twoFactorService(transport),
	steamClock(clock),
	accountLibrary(library),
	record(std::move(loginRecord)) {
	pollTimer.setSingleShot(true);
	connect(&pollTimer, &QTimer::timeout, this, &SteamLogin::Poll);
}

void SteamLogin::Start(const QString& accountName, QString password) {
	if (stage != LoginStage::IDLE) {
		Wipe(password);
		return;
	}
	enteredAccountName = accountName.trimmed();
	SetStage(LoginStage::STARTING);
	authenticationService.GetPasswordRsaPublicKey(enteredAccountName, *this, [this, enteredPassword = std::move(password)](std::expected<PasswordRsaKey, WebApiError> key) mutable {
		if (IsFinished()) {
			Wipe(enteredPassword);
			return;
		}
		if (!key.has_value()) {
			Wipe(enteredPassword);
			FailWithResponseError(key.error());
			return;
		}
		BeginSession(*key, std::move(enteredPassword));
	});
}

void SteamLogin::SubmitCode(const QString& code) {
	if (stage == LoginStage::AWAITING_EMAIL_CODE) {
		SubmitGuardCode(code.trimmed(), AuthGuardType::EMAIL_CODE, LoginStage::AWAITING_EMAIL_CODE, false);
	} else if (stage == LoginStage::AWAITING_DEVICE_CODE) {
		SubmitGuardCode(code.trimmed().toUpper(), AuthGuardType::DEVICE_CODE, LoginStage::AWAITING_DEVICE_CODE, false);
	} else if (stage == LoginStage::AWAITING_MOVE_CODE) {
		FinishGuardMove(code.trimmed());
	}
}

void SteamLogin::StartGuardMove() {
	const LoginStage stageBefore = stage;
	if ((stageBefore != LoginStage::AWAITING_DEVICE_CODE && stageBefore != LoginStage::AWAITING_MOVE_CODE) || authSession.weakToken.isEmpty()) {
		return;
	}
	moveError.reset();
	SetStage(LoginStage::STARTING);
	twoFactorService.StartAuthenticatorMove(authSession.weakToken, *this, [this, stageBefore](const std::expected<void, WebApiError>& started) {
		if (IsFinished()) {
			return;
		}
		if (!started.has_value()) {
			// Without a phone on the account Steam refuses the move; the sign-in itself can still go on.
			moveError = started.error();
			SetStage(stageBefore);
			return;
		}
		isCodeRejected = false;
		SetStage(LoginStage::AWAITING_MOVE_CODE);
	});
}

void SteamLogin::Cancel() {
	if (IsFinished()) {
		return;
	}
	pollTimer.stop();
	isPolling = false;
	SetStage(LoginStage::CANCELLED);
}

LoginStage SteamLogin::Stage() const {
	return stage;
}

const std::optional<LoginFailure>& SteamLogin::Failure() const {
	return failure;
}

const QString& SteamLogin::EmailDomain() const {
	return emailDomain;
}

bool SteamLogin::IsCodeRejected() const {
	return isCodeRejected;
}

bool SteamLogin::IsAwaitingConfirmationToo() const {
	return isPolling && (stage == LoginStage::AWAITING_EMAIL_CODE || stage == LoginStage::AWAITING_DEVICE_CODE);
}

bool SteamLogin::CanMoveGuard() const {
	return stage == LoginStage::AWAITING_DEVICE_CODE && !authSession.weakToken.isEmpty();
}

const std::optional<WebApiError>& SteamLogin::MoveError() const {
	return moveError;
}

const std::optional<AuthenticatorRecord>& SteamLogin::MovedRecord() const {
	return movedRecord;
}

const AuthenticatorRecord& SteamLogin::Record() const {
	return record;
}

void SteamLogin::BeginSession(const PasswordRsaKey& key, QString password) {
	// node-bignumber, which node-steam-session uses for this step, encrypts the UTF-8 bytes of the password.
	QByteArray passwordBytes = password.toUtf8();
	Wipe(password);
	std::optional<QByteArray> encryptedPassword = EncryptRsaPkcs1V15(passwordBytes, key.modulus, key.publicExponent);
	Wipe(passwordBytes);
	if (!encryptedPassword.has_value()) {
		Fail(LoginFailureKind::ENCRYPTION_FAILED);
		return;
	}
	authenticationService.BeginAuthSessionViaCredentials(enteredAccountName, *encryptedPassword, key.timestamp, *this, [this](const std::expected<CredentialsAuthSession, WebApiError>& session) {
		if (IsFinished()) {
			return;
		}
		if (!session.has_value()) {
			FailWithResponseError(session.error());
			return;
		}
		HandleSessionStarted(*session);
	});
}

void SteamLogin::HandleSessionStarted(const CredentialsAuthSession& session) {
	// Never continue with tokens of another account than the one this record belongs to.
	if (record.account.steamId.has_value() && *record.account.steamId != session.steamId) {
		Fail(LoginFailureKind::WRONG_ACCOUNT);
		return;
	}
	authSession = session;
	if (accountLibrary.FindRecord(record.id) == nullptr) {
		AdoptStoredRecord(session.steamId);
	}

	const bool canConfirmElsewhere = Allows(session, AuthGuardType::DEVICE_CONFIRMATION) || Allows(session, AuthGuardType::EMAIL_CONFIRMATION);
	if (Allows(session, AuthGuardType::NONE)) {
		StartPolling();
	} else if (Allows(session, AuthGuardType::DEVICE_CODE) && !record.secrets.sharedSecret.isEmpty()) {
		SubmitGeneratedCode();
	} else if (Allows(session, AuthGuardType::EMAIL_CODE)) {
		const auto emailCode = std::ranges::find(session.allowedConfirmations, AuthGuardType::EMAIL_CODE, &AllowedConfirmation::type);
		emailDomain = emailCode->message;
		SetStage(LoginStage::AWAITING_EMAIL_CODE);
		if (canConfirmElsewhere) {
			StartPolling();
		}
	} else if (Allows(session, AuthGuardType::DEVICE_CODE)) {
		SetStage(LoginStage::AWAITING_DEVICE_CODE);
		if (canConfirmElsewhere) {
			StartPolling();
		}
	} else if (canConfirmElsewhere) {
		SetStage(LoginStage::AWAITING_CONFIRMATION);
		StartPolling();
	} else {
		Fail(LoginFailureKind::UNSUPPORTED_CONFIRMATION);
	}
}

void SteamLogin::AdoptStoredRecord(quint64 steamId) {
	const AuthenticatorRecord* storedRecord = nullptr;
	for (const AuthenticatorRecord& candidate : accountLibrary.Records()) {
		if (candidate.account.steamId != steamId) {
			continue;
		}
		// A record that can generate codes signs in without asking for one.
		if (storedRecord == nullptr || (DetermineCapabilities(candidate).canGenerateCodes && !DetermineCapabilities(*storedRecord).canGenerateCodes)) {
			storedRecord = &candidate;
		}
	}
	if (storedRecord != nullptr) {
		record = *storedRecord;
	} else {
		record.account.steamId = steamId;
	}
}

void SteamLogin::FinishGuardMove(const QString& smsCode) {
	SetStage(LoginStage::STARTING);
	twoFactorService.FinishAuthenticatorMove(authSession.weakToken, smsCode, *this, [this](const std::expected<MovedAuthenticator, WebApiError>& moved) {
		if (IsFinished()) {
			return;
		}
		if (!moved.has_value()) {
			if (moved.error().kind == WebApiErrorKind::STEAM_RESULT && moved.error().steamResult == SteamResult::SMS_CODE_FAILED) {
				isCodeRejected = true;
				SetStage(LoginStage::AWAITING_MOVE_CODE);
				return;
			}
			FailWithResponseError(moved.error());
			return;
		}
		SaveMovedGuard(*moved);
	});
}

void SteamLogin::SaveMovedGuard(const MovedAuthenticator& movedAuthenticator) {
	const QDateTime now = QDateTime::currentDateTimeUtc();
	AuthenticatorRecord moved = record;
	const AuthenticatorRecord* storedRecord = accountLibrary.FindRecord(record.id);
	if (storedRecord == nullptr || !storedRecord->secrets.sharedSecret.isEmpty()) {
		// Secrets of another authenticator generation stay in their own record and are never overwritten.
		moved.id = storedRecord == nullptr ? record.id : QUuid::createUuid();
		moved.label.clear();
		moved.additionalFields = {};
		moved.createdAt = now;
	}
	moved.account.steamId = authSession.steamId;
	if (!movedAuthenticator.accountName.isEmpty()) {
		moved.account.accountName = movedAuthenticator.accountName;
	} else if (moved.account.accountName.isEmpty()) {
		moved.account.accountName = enteredAccountName;
	}
	moved.secrets = movedAuthenticator.secrets;
	moved.secrets.deviceId = GenerateDeviceId();
	moved.pendingActivation.reset();
	moved.source = RecordSource{
		.origin = RecordOrigin::MOVED_TO_STEAMFORGE,
		.fileName = {},
		.importedAt = now,
	};
	movedRecord = moved;
	// The previous authenticator stopped working on the server, so the new secrets are saved before anything else.
	if (const std::expected<void, StorageError> saved = accountLibrary.Save(moved); !saved.has_value()) {
		Fail(LoginFailureKind::GUARD_NOT_SAVED);
		return;
	}
	record = std::move(moved);
	isCodeRejected = false;
	pollTimer.stop();
	isPolling = false;
	SetStage(LoginStage::GUARD_MOVED);
	Q_EMIT GuardMoved();
}

void SteamLogin::SubmitGeneratedCode() {
	const QString code = GenerateGuardCode(record.secrets.sharedSecret, std::chrono::floor<std::chrono::seconds>(steamClock.Now()));
	SubmitGuardCode(code, AuthGuardType::DEVICE_CODE, LoginStage::AWAITING_DEVICE_CODE, true);
}

void SteamLogin::SubmitGuardCode(const QString& code, AuthGuardType codeType, LoginStage stageOnRejection, bool isGeneratedFromRecord) {
	SetStage(LoginStage::STARTING);
	authenticationService.UpdateAuthSessionWithSteamGuardCode(authSession.clientId, authSession.steamId, code, codeType, *this, [this, stageOnRejection, isGeneratedFromRecord](const std::expected<void, WebApiError>& submitted) {
		if (IsFinished()) {
			return;
		}
		if (!submitted.has_value()) {
			if (!IsCodeRejection(submitted.error())) {
				FailWithResponseError(submitted.error());
				return;
			}
			isCodeRejected = true;
			SetStage(stageOnRejection);
			if (Allows(authSession, AuthGuardType::DEVICE_CONFIRMATION) || Allows(authSession, AuthGuardType::EMAIL_CONFIRMATION)) {
				StartPolling();
			}
			return;
		}
		isCodeRejected = false;
		isRecordKeyConfirmed = isGeneratedFromRecord;
		StartPolling();
		Poll();
	});
}

void SteamLogin::StartPolling() {
	if (isPolling) {
		return;
	}
	isPolling = true;
	pollDeadline = std::chrono::steady_clock::now() + POLL_TIMEOUT;
	Poll();
}

void SteamLogin::Poll() {
	if (IsFinished() || isPollRequestInFlight) {
		return;
	}
	if (std::chrono::steady_clock::now() >= pollDeadline) {
		Fail(LoginFailureKind::TIMED_OUT);
		return;
	}
	isPollRequestInFlight = true;
	authenticationService.PollAuthSessionStatus(authSession.clientId, authSession.requestId, *this, [this](const std::expected<AuthSessionPollResult, WebApiError>& result) {
		isPollRequestInFlight = false;
		if (IsFinished()) {
			return;
		}
		if (!result.has_value()) {
			FailWithResponseError(result.error());
			return;
		}
		HandlePollResult(*result);
	});
}

void SteamLogin::HandlePollResult(const AuthSessionPollResult& result) {
	if (result.newClientId.has_value()) {
		authSession.clientId = *result.newClientId;
	}
	if (result.refreshToken.isEmpty()) {
		pollTimer.start(std::max(authSession.pollInterval, SHORTEST_POLL_INTERVAL));
		return;
	}
	pollTimer.stop();
	isPolling = false;
	signedInAccountName = result.accountName.isEmpty() ? enteredAccountName : result.accountName;
	FinishWithTokens(result.refreshToken, result.accessToken);
}

void SteamLogin::FinishWithTokens(const QString& refreshToken, const QString& accessToken) {
	SetStage(LoginStage::FINISHING);
	if (accessToken.isEmpty()) {
		// Steam does not always issue an access token together with the refresh token.
		authenticationService.GenerateAccessTokenForApp(refreshToken, authSession.steamId, false, *this, [this, refreshToken](const std::expected<AppAccessTokens, WebApiError>& tokens) {
			if (IsFinished()) {
				return;
			}
			if (!tokens.has_value()) {
				FailWithResponseError(tokens.error());
				return;
			}
			FinishWithTokens(tokens->refreshToken.isEmpty() ? refreshToken : tokens->refreshToken, tokens->accessToken);
		});
		return;
	}

	const std::optional<JwtClaims> refreshClaims = ReadJwtClaims(refreshToken);
	const std::optional<JwtClaims> accessClaims = ReadJwtClaims(accessToken);
	const bool areTokensForThisAccount = refreshClaims.has_value() && accessClaims.has_value() && refreshClaims->steamId == authSession.steamId && accessClaims->steamId == authSession.steamId;
	if (!areTokensForThisAccount || !HasAudience(*accessClaims, MOBILE_TOKEN_AUDIENCE)) {
		Fail(LoginFailureKind::INVALID_TOKENS);
		return;
	}

	SaveSignIn({
		.recordId = record.id,
		.steamId = authSession.steamId,
		.accountName = signedInAccountName,
		.refreshToken = refreshToken,
		.accessToken = accessToken,
		.signedInAt = QDateTime::currentDateTimeUtc(),
	});
}

void SteamLogin::SaveSignIn(const AccountSession& session) {
	const AuthenticatorRecord* storedRecord = accountLibrary.FindRecord(record.id);
	if (storedRecord != nullptr && isRecordKeyConfirmed && !storedRecord->account.steamId.has_value()) {
		// An imported entry without a SteamID learns it, so confirmations of the account can be signed.
		AuthenticatorRecord identifiedRecord = *storedRecord;
		identifiedRecord.account.steamId = authSession.steamId;
		if (identifiedRecord.account.accountName.isEmpty()) {
			identifiedRecord.account.accountName = signedInAccountName;
		}
		if (const std::expected<void, StorageError> saved = accountLibrary.Save(identifiedRecord); !saved.has_value()) {
			Fail(LoginFailureKind::STORAGE_ERROR);
			return;
		}
		record = std::move(identifiedRecord);
	} else if (storedRecord == nullptr) {
		const QDateTime now = QDateTime::currentDateTimeUtc();
		record.account.steamId = authSession.steamId;
		record.account.accountName = signedInAccountName;
		record.createdAt = now;
		record.source = RecordSource{
			.origin = RecordOrigin::STEAM_SIGN_IN,
			.fileName = {},
			.importedAt = now,
		};
		if (const std::expected<void, StorageError> added = accountLibrary.Add(record); !added.has_value()) {
			Fail(LoginFailureKind::STORAGE_ERROR);
			return;
		}
	}
	if (const std::expected<void, StorageError> saved = accountLibrary.SaveSession(session); !saved.has_value()) {
		Fail(LoginFailureKind::STORAGE_ERROR);
		return;
	}
	SetStage(LoginStage::SUCCEEDED);
	Q_EMIT Succeeded(session);
}

void SteamLogin::SetStage(LoginStage newStage) {
	stage = newStage;
	Q_EMIT StageChanged();
}

void SteamLogin::Fail(LoginFailureKind kind, const WebApiError& error) {
	pollTimer.stop();
	isPolling = false;
	failure = LoginFailure{
		.kind = kind,
		.error = error,
	};
	SetStage(LoginStage::FAILED);
}

void SteamLogin::FailWithResponseError(const WebApiError& error) {
	Fail(ClassifyError(error), error);
}

bool SteamLogin::IsFinished() const {
	return stage == LoginStage::SUCCEEDED || stage == LoginStage::GUARD_MOVED || stage == LoginStage::FAILED || stage == LoginStage::CANCELLED;
}

} // namespace SteamForge
