#pragma once

namespace SteamForge {

enum class RecordOrigin {
	MA_FILE,
	SDA_ENCRYPTED_MA_FILE,
	NODE_STEAMCOMMUNITY_FILE,
	OTPAUTH_URI,
	// Created by signing in; the secrets, if any, were added later in SteamForge.
	STEAM_SIGN_IN,
	ADDED_IN_STEAMFORGE,
	MOVED_TO_STEAMFORGE
};

enum class ImportMatch {
	NEW_ACCOUNT,
	SAME_AUTHENTICATOR,
	OTHER_AUTHENTICATOR_FOR_ACCOUNT
};

struct SteamAccountIdentity {
	std::optional<quint64> steamId;
	QString accountName;
};

// Secrets of one authenticator; fields of different bundles are never combined.
struct SecretBundle {
	QByteArray sharedSecret;
	QByteArray identitySecret;
	QString revocationCode;
	QString deviceId;
	QString serialNumber;
	QString tokenGid;
};

// Steam created the authenticator, but it works only after the activation code is confirmed.
struct PendingActivation {
	int confirmType = 0;
	QString phoneNumberHint;
};

struct RecordSource {
	RecordOrigin origin = RecordOrigin::MA_FILE;
	QString fileName;
	QDateTime importedAt;
};

// The SteamID identifies the account, not the record: one account may have several records.
struct AuthenticatorRecord {
	QUuid id;
	// Display name when it differs from the login, for example a WinAuth entry name.
	QString label;
	SteamAccountIdentity account;
	SecretBundle secrets;
	std::optional<PendingActivation> pendingActivation;
	RecordSource source;
	// Imported fields SteamForge does not interpret, kept unchanged.
	QJsonObject additionalFields;
	QDateTime createdAt;
};

struct RecordCapabilities {
	bool canGenerateCodes = false;
	bool canSignConfirmations = false;
	bool hasRevocationCode = false;
};

[[nodiscard]] RecordCapabilities DetermineCapabilities(const AuthenticatorRecord& record);

[[nodiscard]] bool IsIndividualSteamId(quint64 steamId);

[[nodiscard]] ImportMatch MatchRecord(const AuthenticatorRecord& candidate, std::span<const AuthenticatorRecord> existingRecords);

// The saved record with the fields an import of the same authenticator (equal code key) adds. Nothing when
// the import adds nothing, disagrees with a saved field or the saved setup is unfinished; saved values never change.
[[nodiscard]] std::optional<AuthenticatorRecord> CompleteRecord(const AuthenticatorRecord& savedRecord, const AuthenticatorRecord& importedRecord);

// Values typed by the user; an empty one keeps the saved value.
struct ManualRecordData {
	QString steamId;
	QString identitySecret;
	QString deviceId;
	QString revocationCode;
};

enum class ManualRecordDataError {
	INVALID_STEAM_ID,
	INVALID_IDENTITY_SECRET,
	IDENTITY_SECRET_IS_CODE_KEY,
	INVALID_DEVICE_ID,
	INVALID_REVOCATION_CODE
};

// The SteamID and the recovery code are only added when the record has none; the code key is never touched.
[[nodiscard]] std::expected<AuthenticatorRecord, ManualRecordDataError> ApplyManualRecordData(const AuthenticatorRecord& record, const ManualRecordData& data);

} // namespace SteamForge
