#include "app/login_approval.h"

#include "app/account_library.h"
#include "app/session_access.h"

namespace SteamForge {

namespace {
	// The only request version whose signed layout is known (APK 3.10.9 ParseQRData, node-steam-session LoginApprover).
	constexpr int SUPPORTED_REQUEST_VERSION = 1;
} // namespace

LoginApproval::LoginApproval(WebApiTransport& transport, AccountLibrary& library, AuthenticatorRecord approvingRecord, AccountSession approvingSession, QObject* parent) :
	QObject(parent),
	authenticationService(transport),
	accountLibrary(library),
	record(std::move(approvingRecord)),
	session(std::move(approvingSession)) {
}

void LoginApproval::ProcessScreenImages(const QList<QImage>& images) {
	if (!BeginRequest()) {
		return;
	}
	std::vector<QrChallenge> challenges;
	for (const QImage& image : images) {
		for (const QrChallenge& foundChallenge : FindQrChallenges(image)) {
			if (std::ranges::find(challenges, foundChallenge) == challenges.end()) {
				challenges.push_back(foundChallenge);
			}
		}
	}
	if (challenges.empty()) {
		Fail(LoginApprovalFailureKind::NO_QR_FOUND);
		return;
	}
	// Approving one of several requests by guess could sign in a window the user did not mean.
	if (challenges.size() > 1) {
		Fail(LoginApprovalFailureKind::SEVERAL_QR_FOUND);
		return;
	}
	if (challenges.front().version != SUPPORTED_REQUEST_VERSION) {
		Fail(LoginApprovalFailureKind::UNSUPPORTED_VERSION);
		return;
	}
	const QrChallenge challenge = challenges.front();
	WithAccessToken([this, challenge]() {
		LoadDetails(challenge.clientId, challenge.version);
	});
}

void LoginApproval::CheckPendingRequests() {
	if (!BeginRequest()) {
		return;
	}
	WithAccessToken([this]() {
		authenticationService.GetAuthSessionsForAccount(session.accessToken, *this, [this](const std::expected<std::vector<quint64>, WebApiError>& clientIds) {
			if (IsFinished()) {
				return;
			}
			if (!clientIds.has_value()) {
				FailWithResponseError(clientIds.error());
				return;
			}
			if (clientIds->empty()) {
				SetStage(LoginApprovalStage::NOTHING_PENDING);
				return;
			}
			otherPendingRequestCount = clientIds->size() - 1;
			LoadDetails(clientIds->front(), std::nullopt);
		});
	});
}

void LoginApproval::Approve(bool isSignInRemembered) {
	if (stage == LoginApprovalStage::AWAITING_DECISION) {
		SendDecision(true, isSignInRemembered);
	}
}

void LoginApproval::Deny() {
	if (stage == LoginApprovalStage::AWAITING_DECISION) {
		SendDecision(false, false);
	}
}

void LoginApproval::Cancel() {
	if (!IsFinished() && stage != LoginApprovalStage::SENDING) {
		SetStage(LoginApprovalStage::CANCELLED);
	}
}

LoginApprovalStage LoginApproval::Stage() const {
	return stage;
}

const std::optional<LoginApprovalFailure>& LoginApproval::Failure() const {
	return failure;
}

const AuthSessionInfo& LoginApproval::SessionInfo() const {
	return sessionInfo;
}

std::size_t LoginApproval::OtherPendingRequestCount() const {
	return otherPendingRequestCount;
}

bool LoginApproval::IsPasswordSignIn() const {
	return isPasswordSignIn;
}

bool LoginApproval::BeginRequest() {
	if (stage != LoginApprovalStage::IDLE) {
		return false;
	}
	SetStage(LoginApprovalStage::SEARCHING);
	if (record.secrets.sharedSecret.isEmpty()) {
		Fail(LoginApprovalFailureKind::MISSING_CODE_KEY);
		return false;
	}
	return true;
}

void LoginApproval::WithAccessToken(std::function<void()> continuation) {
	SetStage(LoginApprovalStage::LOADING_DETAILS);
	EnsureFreshAccessToken(authenticationService, accountLibrary, session, *this, [this, continueWithToken = std::move(continuation)](const std::expected<void, SessionAccessFailure>& access) {
		if (IsFinished()) {
			return;
		}
		if (!access.has_value()) {
			switch (access.error().kind) {
				case SessionAccessFailureKind::SIGN_IN_REQUIRED:
					Fail(LoginApprovalFailureKind::SIGN_IN_REQUIRED, access.error().error);
					return;
				case SessionAccessFailureKind::STORAGE_ERROR:
					Fail(LoginApprovalFailureKind::STORAGE_ERROR);
					return;
				case SessionAccessFailureKind::RESPONSE_ERROR:
					FailWithResponseError(access.error().error);
					return;
			}
		}
		continueWithToken();
	});
}

void LoginApproval::LoadDetails(quint64 clientId, std::optional<int> knownVersion) {
	authenticationService.GetAuthSessionInfo(session.accessToken, clientId, *this, [this, clientId, knownVersion](const std::expected<AuthSessionInfo, WebApiError>& info) {
		if (IsFinished()) {
			return;
		}
		if (!info.has_value()) {
			FailWithResponseError(info.error());
			return;
		}
		sessionInfo = *info;
		// A QR code carries its version; a request waiting after a password sign-in reports it in its details.
		request = QrChallenge{
			.version = knownVersion.value_or(info->version),
			.clientId = clientId,
		};
		isPasswordSignIn = !knownVersion.has_value();
		if (request.version != SUPPORTED_REQUEST_VERSION) {
			Fail(LoginApprovalFailureKind::UNSUPPORTED_VERSION);
			return;
		}
		SetStage(LoginApprovalStage::AWAITING_DECISION);
	});
}

void LoginApproval::SendDecision(bool isApproved, bool isSignInRemembered) {
	SetStage(LoginApprovalStage::SENDING);
	const MobileConfirmation confirmation{
		.version = request.version,
		.clientId = request.clientId,
		.steamId = session.steamId,
		.signature = SignQrChallenge(request, session.steamId, record.secrets.sharedSecret),
		.isApproved = isApproved,
		.isPersistent = isSignInRemembered,
	};
	authenticationService.UpdateAuthSessionWithMobileConfirmation(session.accessToken, confirmation, *this, [this, isApproved](const std::expected<void, WebApiError>& sent) {
		if (!sent.has_value()) {
			FailWithResponseError(sent.error());
			return;
		}
		SetStage(isApproved ? LoginApprovalStage::APPROVED : LoginApprovalStage::DENIED);
	});
}

void LoginApproval::SetStage(LoginApprovalStage newStage) {
	stage = newStage;
	Q_EMIT StageChanged();
}

void LoginApproval::Fail(LoginApprovalFailureKind kind, const WebApiError& error) {
	failure = LoginApprovalFailure{
		.kind = kind,
		.error = error,
	};
	SetStage(LoginApprovalStage::FAILED);
}

void LoginApproval::FailWithResponseError(const WebApiError& error) {
	if (IsSignInRequired(error)) {
		Fail(LoginApprovalFailureKind::SIGN_IN_REQUIRED, error);
	} else if (error.kind == WebApiErrorKind::STEAM_RESULT && error.steamResult == SteamResult::FILE_NOT_FOUND) {
		Fail(LoginApprovalFailureKind::REQUEST_ENDED, error);
	} else if (error.kind == WebApiErrorKind::NETWORK || error.kind == WebApiErrorKind::TIMEOUT) {
		Fail(LoginApprovalFailureKind::CONNECTION, error);
	} else {
		Fail(LoginApprovalFailureKind::STEAM_ERROR, error);
	}
}

bool LoginApproval::IsFinished() const {
	return stage == LoginApprovalStage::APPROVED || stage == LoginApprovalStage::DENIED || stage == LoginApprovalStage::NOTHING_PENDING || stage == LoginApprovalStage::FAILED || stage == LoginApprovalStage::CANCELLED;
}

} // namespace SteamForge
