#pragma once

#include "domain/authenticator_record.h"

namespace SteamForge {

// A .maFile as Steam Desktop Authenticator keeps it, without encryption. The session carries only the SteamID:
// sign-in tokens never leave SteamForge, so the other program signs in by itself.
[[nodiscard]] QByteArray ExportMaFile(const AuthenticatorRecord& record);

// The otpauth link of the Steam Guard key, the same as the uri field of a .maFile.
[[nodiscard]] QString SteamGuardUri(const AuthenticatorRecord& record);

// How an authenticator app recognizes Steam in an otpauth link; each app reads one of the two forms.
enum class SteamGuardLinkStyle {
	// otpauth://totp/ with encoder=steam: Stratum, 2FAS for iPhone, KeePassDX.
	STEAM_ENCODER,
	// otpauth://steam/: Aegis, Ente, 2FAS for Android.
	STEAM_TYPE
};

// The link for an authenticator app. digits=5, because 2FAS for iPhone otherwise makes six-character codes.
[[nodiscard]] QString SteamGuardAppUri(const AuthenticatorRecord& record, SteamGuardLinkStyle style);

// The file name Steam Desktop Authenticator gives an account: its SteamID, or the account name without one.
[[nodiscard]] QString MaFileName(const AuthenticatorRecord& record);

} // namespace SteamForge
