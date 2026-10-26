#pragma once

#include "domain/authenticator_record.h"
#include "steam/web_api_transport.h"

namespace SteamForge {

// The confirm type APK 3.10.9 (useTriggerAddAuthenticator) answers with an email code; every other value means an SMS code.
inline constexpr int EMAIL_ACTIVATION_CONFIRM_TYPE = 3;

struct AddedAuthenticator {
	// Without the device ID, which the caller chooses.
	SecretBundle secrets;
	QString accountName;
	QString phoneNumberHint;
	int confirmType = 0;
};

struct FinalizeOutcome {
	bool isSuccess = false;
	int status = 0;
};

struct MovedAuthenticator {
	SecretBundle secrets;
	quint64 steamId = 0;
	QString accountName;
};

// Typed ITwoFactorService calls as the Steam mobile application makes them.
class TwoFactorService final {
public:
	explicit TwoFactorService(WebApiTransport& webApiTransport);

	void AddAuthenticator(const QString& accessToken, quint64 steamId, const QString& deviceId, QObject& context, WebApiHandler<AddedAuthenticator> handler);
	void FinalizeAddAuthenticator(const QString& accessToken, quint64 steamId, const QString& authenticatorCode, std::chrono::sys_seconds authenticatorTime, const QString& activationCode, QObject& context, WebApiHandler<FinalizeOutcome> handler);
	// Steam sends an SMS code to the phone of the account; the weak token comes from the sign-in that is waiting for a code.
	void StartAuthenticatorMove(const QString& weakToken, QObject& context, WebApiHandler<void> handler);
	void FinishAuthenticatorMove(const QString& weakToken, const QString& smsCode, QObject& context, WebApiHandler<MovedAuthenticator> handler);

private:
	WebApiTransport& transport;
};

} // namespace SteamForge
