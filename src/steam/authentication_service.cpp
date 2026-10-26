#include "steam/authentication_service.h"

#include "steam/web_api_call.h"

#include "steam_authentication.pb.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView AUTHENTICATION_INTERFACE{"Authentication"};
	constexpr QLatin1StringView GET_PASSWORD_RSA_PUBLIC_KEY_METHOD{"GetPasswordRSAPublicKey"};
	constexpr QLatin1StringView BEGIN_AUTH_SESSION_VIA_CREDENTIALS_METHOD{"BeginAuthSessionViaCredentials"};
	constexpr QLatin1StringView UPDATE_AUTH_SESSION_WITH_STEAM_GUARD_CODE_METHOD{"UpdateAuthSessionWithSteamGuardCode"};
	constexpr QLatin1StringView POLL_AUTH_SESSION_STATUS_METHOD{"PollAuthSessionStatus"};
	constexpr QLatin1StringView GENERATE_ACCESS_TOKEN_FOR_APP_METHOD{"GenerateAccessTokenForApp"};
	constexpr QLatin1StringView GET_AUTH_SESSIONS_FOR_ACCOUNT_METHOD{"GetAuthSessionsForAccount"};
	constexpr QLatin1StringView GET_AUTH_SESSION_INFO_METHOD{"GetAuthSessionInfo"};
	constexpr QLatin1StringView UPDATE_AUTH_SESSION_WITH_MOBILE_CONFIRMATION_METHOD{"UpdateAuthSessionWithMobileConfirmation"};
	constexpr int METHOD_VERSION = 1;

	// Platform data the Steam mobile application 3.10.9 sends (GetMobileDeviceDetails); tokens for
	// confirmations and QR approval are issued only to the MobileApp platform.
	constexpr int MOBILE_APP_PLATFORM_TYPE = 3;
	constexpr int MOBILE_STEAM_APP_TYPE = 1;
	constexpr int ANDROID_UNKNOWN_OS_TYPE = -500;
	constexpr unsigned int PHONE_GAMING_DEVICE_TYPE = 528;
	constexpr int EPHEMERAL_SESSION = 0;
	constexpr int PERSISTENT_SESSION = 1;
	constexpr int TOKEN_RENEWAL_NONE = 0;
	constexpr int TOKEN_RENEWAL_ALLOW = 1;
	constexpr std::string_view MOBILE_WEBSITE_ID{"Mobile"};
	constexpr std::string_view DEVICE_FRIENDLY_NAME{"SteamForge"};
	constexpr double MILLISECONDS_PER_SECOND = 1000.0;

	WebApiRequest AuthenticationRequest(QLatin1StringView methodName, QByteArray body, WebApiHttpMethod httpMethod = WebApiHttpMethod::POST, const QString& accessToken = {}) {
		return {
			.interfaceName = AUTHENTICATION_INTERFACE,
			.methodName = methodName,
			.version = METHOD_VERSION,
			.httpMethod = httpMethod,
			.protobufBody = std::move(body),
			.accessToken = accessToken,
			.encoding = WebApiEncoding::PROTOBUF,
			.jsonParameters = {},
		};
	}
} // namespace

AuthenticationService::AuthenticationService(WebApiTransport& webApiTransport) :
	transport(webApiTransport) {
}

void AuthenticationService::GetPasswordRsaPublicKey(const QString& accountName, QObject& context, AuthenticationHandler<PasswordRsaKey> handler) {
	SteamProtocol::GetPasswordRsaPublicKeyRequest request;
	request.set_account_name(accountName.toStdString());
	CallWebApi<SteamProtocol::GetPasswordRsaPublicKeyResponse>(transport, AuthenticationRequest(GET_PASSWORD_RSA_PUBLIC_KEY_METHOD, SerializeMessage(request), WebApiHttpMethod::GET), context, std::move(handler), [](const SteamProtocol::GetPasswordRsaPublicKeyResponse& response) -> std::expected<PasswordRsaKey, WebApiError> {
		PasswordRsaKey key{
			.modulus = QByteArray::fromHex(QByteArray::fromStdString(response.publickey_mod())),
			.publicExponent = QByteArray::fromHex(QByteArray::fromStdString(response.publickey_exp())),
			.timestamp = response.timestamp(),
		};
		if (key.modulus.isEmpty() || key.publicExponent.isEmpty()) {
			return std::unexpected(MalformedWebApiResponse());
		}
		return key;
	});
}

void AuthenticationService::BeginAuthSessionViaCredentials(const QString& accountName, const QByteArray& encryptedPassword, quint64 encryptionTimestamp, QObject& context, AuthenticationHandler<CredentialsAuthSession> handler) {
	SteamProtocol::BeginAuthSessionViaCredentialsRequest request;
	request.set_account_name(accountName.toStdString());
	request.set_encrypted_password(encryptedPassword.toBase64().toStdString());
	request.set_encryption_timestamp(encryptionTimestamp);
	request.set_remember_login(true);
	request.set_platform_type(MOBILE_APP_PLATFORM_TYPE);
	request.set_persistence(PERSISTENT_SESSION);
	request.set_website_id(std::string(MOBILE_WEBSITE_ID));
	SteamProtocol::DeviceDetails* deviceDetails = request.mutable_device_details();
	deviceDetails->set_device_friendly_name(std::string(DEVICE_FRIENDLY_NAME));
	deviceDetails->set_platform_type(MOBILE_APP_PLATFORM_TYPE);
	deviceDetails->set_os_type(ANDROID_UNKNOWN_OS_TYPE);
	deviceDetails->set_gaming_device_type(PHONE_GAMING_DEVICE_TYPE);
	deviceDetails->set_app_type(MOBILE_STEAM_APP_TYPE);

	CallWebApi<SteamProtocol::BeginAuthSessionViaCredentialsResponse>(transport, AuthenticationRequest(BEGIN_AUTH_SESSION_VIA_CREDENTIALS_METHOD, SerializeMessage(request)), context, std::move(handler), [](const SteamProtocol::BeginAuthSessionViaCredentialsResponse& response) -> std::expected<CredentialsAuthSession, WebApiError> {
		if (!response.has_client_id() || !response.has_request_id() || !response.has_steamid()) {
			return std::unexpected(MalformedWebApiResponse());
		}
		CredentialsAuthSession session{
			.clientId = response.client_id(),
			.requestId = QByteArray::fromStdString(response.request_id()),
			.pollInterval = std::chrono::milliseconds(static_cast<std::int64_t>(static_cast<double>(response.interval()) * MILLISECONDS_PER_SECOND)),
			.allowedConfirmations = {},
			.steamId = response.steamid(),
			.weakToken = FromUtf8(response.weak_token()),
		};
		for (const SteamProtocol::AllowedConfirmation& confirmation : response.allowed_confirmations()) {
			session.allowedConfirmations.push_back({
				.type = static_cast<AuthGuardType>(confirmation.confirmation_type()),
				.message = FromUtf8(confirmation.associated_message()),
			});
		}
		return session;
	});
}

void AuthenticationService::UpdateAuthSessionWithSteamGuardCode(quint64 clientId, quint64 steamId, const QString& code, AuthGuardType codeType, QObject& context, AuthenticationHandler<void> handler) {
	SteamProtocol::UpdateAuthSessionWithSteamGuardCodeRequest request;
	request.set_client_id(clientId);
	request.set_steamid(steamId);
	request.set_code(code.toStdString());
	request.set_code_type(static_cast<int>(codeType));
	CallWebApiWithoutResult(transport, AuthenticationRequest(UPDATE_AUTH_SESSION_WITH_STEAM_GUARD_CODE_METHOD, SerializeMessage(request)), context, std::move(handler));
}

void AuthenticationService::PollAuthSessionStatus(quint64 clientId, const QByteArray& requestId, QObject& context, AuthenticationHandler<AuthSessionPollResult> handler) {
	SteamProtocol::PollAuthSessionStatusRequest request;
	request.set_client_id(clientId);
	request.set_request_id(requestId.toStdString());
	CallWebApi<SteamProtocol::PollAuthSessionStatusResponse>(transport, AuthenticationRequest(POLL_AUTH_SESSION_STATUS_METHOD, SerializeMessage(request)), context, std::move(handler), [](const SteamProtocol::PollAuthSessionStatusResponse& response) -> std::expected<AuthSessionPollResult, WebApiError> {
		AuthSessionPollResult result{
			.newClientId = std::nullopt,
			.refreshToken = FromUtf8(response.refresh_token()),
			.accessToken = FromUtf8(response.access_token()),
			.accountName = FromUtf8(response.account_name()),
		};
		if (response.has_new_client_id()) {
			result.newClientId = response.new_client_id();
		}
		return result;
	});
}

void AuthenticationService::GenerateAccessTokenForApp(const QString& refreshToken, quint64 steamId, bool isRefreshTokenRenewalAllowed, QObject& context, AuthenticationHandler<AppAccessTokens> handler) {
	SteamProtocol::GenerateAccessTokenForAppRequest request;
	request.set_refresh_token(refreshToken.toStdString());
	request.set_steamid(steamId);
	request.set_renewal_type(isRefreshTokenRenewalAllowed ? TOKEN_RENEWAL_ALLOW : TOKEN_RENEWAL_NONE);
	CallWebApi<SteamProtocol::GenerateAccessTokenForAppResponse>(transport, AuthenticationRequest(GENERATE_ACCESS_TOKEN_FOR_APP_METHOD, SerializeMessage(request)), context, std::move(handler), [](const SteamProtocol::GenerateAccessTokenForAppResponse& response) -> std::expected<AppAccessTokens, WebApiError> {
		if (response.access_token().empty()) {
			return std::unexpected(MalformedWebApiResponse());
		}
		return AppAccessTokens{
			.accessToken = FromUtf8(response.access_token()),
			.refreshToken = FromUtf8(response.refresh_token()),
		};
	});
}

void AuthenticationService::GetAuthSessionsForAccount(const QString& accessToken, QObject& context, AuthenticationHandler<std::vector<quint64>> handler) {
	const SteamProtocol::GetAuthSessionsForAccountRequest request;
	CallWebApi<SteamProtocol::GetAuthSessionsForAccountResponse>(transport, AuthenticationRequest(GET_AUTH_SESSIONS_FOR_ACCOUNT_METHOD, SerializeMessage(request), WebApiHttpMethod::GET, accessToken), context, std::move(handler), [](const SteamProtocol::GetAuthSessionsForAccountResponse& response) -> std::expected<std::vector<quint64>, WebApiError> {
		return std::vector<quint64>(response.client_ids().begin(), response.client_ids().end());
	});
}

void AuthenticationService::GetAuthSessionInfo(const QString& accessToken, quint64 clientId, QObject& context, AuthenticationHandler<AuthSessionInfo> handler) {
	SteamProtocol::GetAuthSessionInfoRequest request;
	request.set_client_id(clientId);
	CallWebApi<SteamProtocol::GetAuthSessionInfoResponse>(transport, AuthenticationRequest(GET_AUTH_SESSION_INFO_METHOD, SerializeMessage(request), WebApiHttpMethod::POST, accessToken), context, std::move(handler), [](const SteamProtocol::GetAuthSessionInfoResponse& response) -> std::expected<AuthSessionInfo, WebApiError> {
		return AuthSessionInfo{
			.ipAddress = FromUtf8(response.ip()),
			.city = FromUtf8(response.city()),
			.state = FromUtf8(response.state()),
			.country = FromUtf8(response.country()),
			.deviceFriendlyName = FromUtf8(response.device_friendly_name()),
			.platformType = response.platform_type(),
			.version = response.version(),
			.isRequestorLocationMismatch = response.requestor_location_mismatch(),
			.isHighUsageLogin = response.high_usage_login(),
			.isPersistenceRequested = response.requested_persistence() == PERSISTENT_SESSION,
		};
	});
}

void AuthenticationService::UpdateAuthSessionWithMobileConfirmation(const QString& accessToken, const MobileConfirmation& confirmation, QObject& context, AuthenticationHandler<void> handler) {
	SteamProtocol::UpdateAuthSessionWithMobileConfirmationRequest request;
	request.set_version(confirmation.version);
	request.set_client_id(confirmation.clientId);
	request.set_steamid(confirmation.steamId);
	request.set_signature(confirmation.signature.toStdString());
	request.set_confirm(confirmation.isApproved);
	request.set_persistence(confirmation.isPersistent ? PERSISTENT_SESSION : EPHEMERAL_SESSION);
	CallWebApiWithoutResult(transport, AuthenticationRequest(UPDATE_AUTH_SESSION_WITH_MOBILE_CONFIRMATION_METHOD, SerializeMessage(request), WebApiHttpMethod::POST, accessToken), context, std::move(handler));
}

} // namespace SteamForge
