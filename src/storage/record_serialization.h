#pragma once

#include "domain/authenticator_record.h"
#include "storage/storage_error.h"

namespace SteamForge {

inline constexpr int RECORD_FORMAT_VERSION = 1;

[[nodiscard]] QJsonObject SerializeRecord(const AuthenticatorRecord& record);

[[nodiscard]] std::expected<AuthenticatorRecord, StorageError> DeserializeRecord(const QJsonObject& document);

} // namespace SteamForge
