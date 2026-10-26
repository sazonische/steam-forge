#include "steam/two_factor_service.h"

#include "steam/web_api_call.h"

#include "steam_two_factor.pb.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView TWO_FACTOR_INTERFACE{"TwoFactor"};
	constexpr QLatin1StringView ADD_AUTHENTICATOR_METHOD{"AddAuthenticator"};
	constexpr QLatin1StringView FINALIZE_ADD_AUTHENTICATOR_METHOD{"FinalizeAddAuthenticator"};
	constexpr QLatin1StringView REMOVE_VIA_CHALLENGE_START_METHOD{"RemoveAuthenticatorViaChallengeStart"};
	constexpr QLatin1StringView REMOVE_VIA_CHALLENGE_CONTINUE_METHOD{"RemoveAuthenticatorViaChallengeContinue"};
	constexpr int METHOD_VERSION = 1;
	// Values APK 3.10.9 sends (useAddAuthenticator, FinishMoveAuthenticator; k_unTwoFactorVersion).
	constexpr unsigned int MOBILE_AUTHENTICATOR_TYPE = 1;
	constexpr unsigned int TWO_FACTOR_VERSION = 2;

	WebApiRequest TwoFactorRequest(QLatin1StringView methodName, QByteArray body, const QString& accessToken) {
		return {
			.interfaceName = TWO_FACTOR_INTERFACE,
			.methodName = methodName,
			.version = METHOD_VERSION,
			.httpMethod = WebApiHttpMethod::POST,
			.protobufBody = std::move(body),
			.accessToken = accessToken,
			.encoding = WebApiEncoding::PROTOBUF,
			.jsonParameters = {},
		};
	}

	WebApiError UnsuccessfulResponse() {
		return WebApiError{
			.kind = WebApiErrorKind::UNSUCCESSFUL_RESPONSE,
			.httpStatus = 0,
			.steamResult = SteamResult::OK,
			.message = {},
		};
	}

	template<typename Message>
	SecretBundle ReadSecrets(const Message& message) {
		return {
			.sharedSecret = QByteArray::fromStdString(message.shared_secret()),
			.identitySecret = QByteArray::fromStdString(message.identity_secret()),
			.revocationCode = FromUtf8(message.revocation_code()),
			.deviceId = {},
			.serialNumber = message.has_serial_number() ? QString::number(message.serial_number()) : QString(),
			.tokenGid = FromUtf8(message.token_gid()),
		};
	}
} // namespace

TwoFactorService::TwoFactorService(WebApiTransport& webApiTransport) :
	transport(webApiTransport) {
}

void TwoFactorService::AddAuthenticator(const QString& accessToken, quint64 steamId, const QString& deviceId, QObject& context, WebApiHandler<AddedAuthenticator> handler) {
	SteamProtocol::AddAuthenticatorRequest request;
	request.set_steamid(steamId);
	request.set_authenticator_type(MOBILE_AUTHENTICATOR_TYPE);
	request.set_device_identifier(deviceId.toStdString());
	request.set_version(TWO_FACTOR_VERSION);
	CallWebApi<SteamProtocol::AddAuthenticatorResponse>(transport, TwoFactorRequest(ADD_AUTHENTICATOR_METHOD, SerializeMessage(request), accessToken), context, std::move(handler), [](const SteamProtocol::AddAuthenticatorResponse& response) -> std::expected<AddedAuthenticator, WebApiError> {
		if (response.shared_secret().empty() || response.identity_secret().empty() || response.revocation_code().empty()) {
			return std::unexpected(MalformedWebApiResponse());
		}
		return AddedAuthenticator{
			.secrets = ReadSecrets(response),
			.accountName = FromUtf8(response.account_name()),
			.phoneNumberHint = FromUtf8(response.phone_number_hint()),
			.confirmType = response.confirm_type(),
		};
	});
}

void TwoFactorService::FinalizeAddAuthenticator(const QString& accessToken, quint64 steamId, const QString& authenticatorCode, std::chrono::sys_seconds authenticatorTime, const QString& activationCode, QObject& context, WebApiHandler<FinalizeOutcome> handler) {
	SteamProtocol::FinalizeAddAuthenticatorRequest request;
	request.set_steamid(steamId);
	request.set_authenticator_code(authenticatorCode.toStdString());
	request.set_authenticator_time(static_cast<quint64>(authenticatorTime.time_since_epoch().count()));
	request.set_activation_code(activationCode.toStdString());
	request.set_validate_sms_code(true);
	CallWebApi<SteamProtocol::FinalizeAddAuthenticatorResponse>(transport, TwoFactorRequest(FINALIZE_ADD_AUTHENTICATOR_METHOD, SerializeMessage(request), accessToken), context, std::move(handler), [](const SteamProtocol::FinalizeAddAuthenticatorResponse& response) -> std::expected<FinalizeOutcome, WebApiError> {
		return FinalizeOutcome{
			.isSuccess = response.success(),
			.status = response.status(),
		};
	});
}

void TwoFactorService::StartAuthenticatorMove(const QString& weakToken, QObject& context, WebApiHandler<void> handler) {
	const SteamProtocol::RemoveAuthenticatorViaChallengeStartRequest request;
	// Only the EResult decides, as in APK 3.10.9 StartMoveAuthenticator, which does not read the body.
	CallWebApiWithoutResult(transport, TwoFactorRequest(REMOVE_VIA_CHALLENGE_START_METHOD, SerializeMessage(request), weakToken), context, std::move(handler));
}

void TwoFactorService::FinishAuthenticatorMove(const QString& weakToken, const QString& smsCode, QObject& context, WebApiHandler<MovedAuthenticator> handler) {
	SteamProtocol::RemoveAuthenticatorViaChallengeContinueRequest request;
	request.set_sms_code(smsCode.toStdString());
	request.set_generate_new_token(true);
	request.set_version(TWO_FACTOR_VERSION);
	CallWebApi<SteamProtocol::RemoveAuthenticatorViaChallengeContinueResponse>(transport, TwoFactorRequest(REMOVE_VIA_CHALLENGE_CONTINUE_METHOD, SerializeMessage(request), weakToken), context, std::move(handler), [](const SteamProtocol::RemoveAuthenticatorViaChallengeContinueResponse& response) -> std::expected<MovedAuthenticator, WebApiError> {
		if (!response.success()) {
			return std::unexpected(UnsuccessfulResponse());
		}
		const SteamProtocol::ReplacementToken& replacement = response.replacement_token();
		if (replacement.shared_secret().empty() || replacement.identity_secret().empty() || replacement.revocation_code().empty()) {
			return std::unexpected(MalformedWebApiResponse());
		}
		return MovedAuthenticator{
			.secrets = ReadSecrets(replacement),
			.steamId = replacement.steamid(),
			.accountName = FromUtf8(replacement.account_name()),
		};
	});
}

} // namespace SteamForge
