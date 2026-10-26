#include "domain/authenticator_record.h"

#include "common/encoding.h"

namespace SteamForge {

namespace {
	// SteamID64 layout: universe (8 bits), account type (4 bits), instance (20 bits), account number (32 bits).
	constexpr int STEAM_ID_UNIVERSE_SHIFT = 56;
	constexpr int STEAM_ID_ACCOUNT_TYPE_SHIFT = 52;
	constexpr quint64 STEAM_ID_ACCOUNT_TYPE_MASK = 0xF;
	constexpr quint64 STEAM_ID_ACCOUNT_NUMBER_MASK = 0xFFFFFFFF;
	constexpr quint64 PUBLIC_UNIVERSE = 1;
	constexpr quint64 INDIVIDUAL_ACCOUNT_TYPE = 1;
	// Steam issues both secrets as 20-byte HMAC-SHA1 keys.
	constexpr qsizetype SECRET_KEY_SIZE = 20;
	constexpr QLatin1StringView DEVICE_ID_PREFIX{"android:"};
	// Steam recovery codes are R followed by five digits.
	constexpr QLatin1StringView REVOCATION_CODE_PATTERN{"^R\\d{5}$"};

	// Fills an empty field from the import; false when both are set and differ.
	template<typename Value, typename Equal>
	bool CompleteField(Value& savedValue, const Value& importedValue, bool& hasAdded, Equal areEqual) {
		const auto isEmpty = [](const Value& value) {
			if constexpr (requires { value.isEmpty(); }) {
				return value.isEmpty();
			} else {
				return !value.has_value();
			}
		};
		if (isEmpty(importedValue)) {
			return true;
		}
		if (isEmpty(savedValue)) {
			savedValue = importedValue;
			hasAdded = true;
			return true;
		}
		return areEqual(savedValue, importedValue);
	}

	template<typename Value>
	bool CompleteField(Value& savedValue, const Value& importedValue, bool& hasAdded) {
		return CompleteField(savedValue, importedValue, hasAdded, std::equal_to<>());
	}

	bool IsSameAccount(const SteamAccountIdentity& first, const SteamAccountIdentity& second) {
		if (first.steamId.has_value() && second.steamId.has_value()) {
			return *first.steamId == *second.steamId;
		}
		// Steam login names are unique, so they identify the account when a SteamID is missing.
		return !first.accountName.isEmpty() && first.accountName.compare(second.accountName, Qt::CaseInsensitive) == 0;
	}
} // namespace

RecordCapabilities DetermineCapabilities(const AuthenticatorRecord& record) {
	const SecretBundle& secrets = record.secrets;
	const bool isActive = !record.pendingActivation.has_value();
	return {
		.canGenerateCodes = isActive && !secrets.sharedSecret.isEmpty(),
		.canSignConfirmations = isActive && !secrets.identitySecret.isEmpty() && !secrets.deviceId.isEmpty() && record.account.steamId.has_value(),
		.hasRevocationCode = !secrets.revocationCode.isEmpty(),
	};
}

bool IsIndividualSteamId(quint64 steamId) {
	const quint64 universe = steamId >> STEAM_ID_UNIVERSE_SHIFT;
	const quint64 accountType = (steamId >> STEAM_ID_ACCOUNT_TYPE_SHIFT) & STEAM_ID_ACCOUNT_TYPE_MASK;
	const quint64 accountNumber = steamId & STEAM_ID_ACCOUNT_NUMBER_MASK;
	return universe == PUBLIC_UNIVERSE && accountType == INDIVIDUAL_ACCOUNT_TYPE && accountNumber != 0;
}

ImportMatch MatchRecord(const AuthenticatorRecord& candidate, std::span<const AuthenticatorRecord> existingRecords) {
	ImportMatch match = ImportMatch::NEW_ACCOUNT;
	for (const AuthenticatorRecord& existingRecord : existingRecords) {
		if (!candidate.secrets.sharedSecret.isEmpty() && candidate.secrets.sharedSecret == existingRecord.secrets.sharedSecret) {
			return ImportMatch::SAME_AUTHENTICATOR;
		}
		if (IsSameAccount(candidate.account, existingRecord.account)) {
			match = ImportMatch::OTHER_AUTHENTICATOR_FOR_ACCOUNT;
		}
	}
	return match;
}

std::optional<AuthenticatorRecord> CompleteRecord(const AuthenticatorRecord& savedRecord, const AuthenticatorRecord& importedRecord) {
	if (savedRecord.pendingActivation.has_value() || importedRecord.pendingActivation.has_value() || savedRecord.secrets.sharedSecret.isEmpty() || savedRecord.secrets.sharedSecret != importedRecord.secrets.sharedSecret) {
		return std::nullopt;
	}
	AuthenticatorRecord completedRecord = savedRecord;
	SecretBundle& secrets = completedRecord.secrets;
	const SecretBundle& importedSecrets = importedRecord.secrets;
	bool hasAdded = false;
	const auto isSameAccountName = [](const QString& savedName, const QString& importedName) {
		return savedName.compare(importedName, Qt::CaseInsensitive) == 0;
	};
	const std::array fieldAgreements{
		CompleteField(completedRecord.account.steamId, importedRecord.account.steamId, hasAdded),
		CompleteField(completedRecord.account.accountName, importedRecord.account.accountName, hasAdded, isSameAccountName),
		CompleteField(secrets.identitySecret, importedSecrets.identitySecret, hasAdded),
		CompleteField(secrets.deviceId, importedSecrets.deviceId, hasAdded),
		CompleteField(secrets.revocationCode, importedSecrets.revocationCode, hasAdded),
		CompleteField(secrets.serialNumber, importedSecrets.serialNumber, hasAdded),
		CompleteField(secrets.tokenGid, importedSecrets.tokenGid, hasAdded),
	};
	if (!std::ranges::all_of(fieldAgreements, std::identity())) {
		return std::nullopt;
	}
	for (auto fieldIterator = importedRecord.additionalFields.begin(); fieldIterator != importedRecord.additionalFields.end(); ++fieldIterator) {
		if (!completedRecord.additionalFields.contains(fieldIterator.key())) {
			completedRecord.additionalFields.insert(fieldIterator.key(), fieldIterator.value());
			hasAdded = true;
		}
	}
	if (!hasAdded) {
		return std::nullopt;
	}
	return completedRecord;
}

std::expected<AuthenticatorRecord, ManualRecordDataError> ApplyManualRecordData(const AuthenticatorRecord& record, const ManualRecordData& data) {
	AuthenticatorRecord updatedRecord = record;
	const QString steamIdText = data.steamId.trimmed();
	if (!record.account.steamId.has_value() && !steamIdText.isEmpty()) {
		bool isNumber = false;
		const quint64 steamId = steamIdText.toULongLong(&isNumber);
		if (!isNumber || !IsIndividualSteamId(steamId)) {
			return std::unexpected(ManualRecordDataError::INVALID_STEAM_ID);
		}
		updatedRecord.account.steamId = steamId;
	}
	if (const QString identitySecretText = data.identitySecret.trimmed(); !identitySecretText.isEmpty()) {
		const std::optional<QByteArray> identitySecret = DecodeBase64(identitySecretText);
		if (!identitySecret.has_value() || identitySecret->size() != SECRET_KEY_SIZE) {
			return std::unexpected(ManualRecordDataError::INVALID_IDENTITY_SECRET);
		}
		// Both keys look the same in a file, and the code key would only make Steam reject every confirmation.
		if (*identitySecret == record.secrets.sharedSecret) {
			return std::unexpected(ManualRecordDataError::IDENTITY_SECRET_IS_CODE_KEY);
		}
		updatedRecord.secrets.identitySecret = *identitySecret;
	}
	if (const QString deviceIdText = data.deviceId.trimmed(); !deviceIdText.isEmpty()) {
		const bool hasPrefix = deviceIdText.startsWith(DEVICE_ID_PREFIX, Qt::CaseInsensitive);
		if (!hasPrefix || QUuid::fromString(QStringView(deviceIdText).sliced(DEVICE_ID_PREFIX.size())).isNull()) {
			return std::unexpected(ManualRecordDataError::INVALID_DEVICE_ID);
		}
		updatedRecord.secrets.deviceId = deviceIdText;
	}
	if (const QString revocationCodeText = data.revocationCode.trimmed().toUpper(); record.secrets.revocationCode.isEmpty() && !revocationCodeText.isEmpty()) {
		if (!QRegularExpression(REVOCATION_CODE_PATTERN).match(revocationCodeText).hasMatch()) {
			return std::unexpected(ManualRecordDataError::INVALID_REVOCATION_CODE);
		}
		updatedRecord.secrets.revocationCode = revocationCodeText;
	}
	return updatedRecord;
}

} // namespace SteamForge
