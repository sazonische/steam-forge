#pragma once

#include "crypto/data_sealing.h"
#include "domain/authenticator_record.h"

namespace SteamForge {

// Names one revision of a record in the cloud vault; all of it is bound into the encryption, so a file moved to
// another vault, record or revision does not open.
struct CloudRecordPlace {
	QString vaultId;
	QString recordId;
	int revision = 1;
};

// Only Steam Guard leaves the computer: Steam sign-ins that old .maFiles carried stay local.
[[nodiscard]] AuthenticatorRecord WithoutSteamSignIn(AuthenticatorRecord record);
// A cloud version keeps the Steam sign-in this computer has for the same account.
[[nodiscard]] AuthenticatorRecord WithLocalSteamSignIn(AuthenticatorRecord cloudRecord, const AuthenticatorRecord& localRecord);

// Tells whether a record changed since it was last synced; the same record gives the same fingerprint.
[[nodiscard]] QString CloudRecordFingerprint(const AuthenticatorRecord& record);

// The file content of a record revision in Google Drive.
[[nodiscard]] std::optional<QByteArray> SealCloudRecord(const DataKey& cloudKey, const CloudRecordPlace& place, const AuthenticatorRecord& record);

enum class CloudRecordError {
	NOT_A_RECORD,
	NEWER_VERSION,
	// Another key, another place or a changed byte.
	UNSEALING_FAILED,
	DAMAGED
};

[[nodiscard]] std::expected<AuthenticatorRecord, CloudRecordError> OpenCloudRecord(const DataKey& cloudKey, const CloudRecordPlace& place, const QByteArray& fileContent);

} // namespace SteamForge
