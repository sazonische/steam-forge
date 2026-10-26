#pragma once

namespace SteamForge {

// Only for reading files of other authenticator programs; SteamForge does not encrypt with these.

[[nodiscard]] std::optional<QByteArray> DeriveKeyPbkdf2HmacSha1(QByteArrayView password, QByteArrayView salt, int iterationCount, int keyLength);

[[nodiscard]] std::optional<QByteArray> DecryptAes256CbcPkcs7(QByteArrayView ciphertext, QByteArrayView key, QByteArrayView initializationVector);

} // namespace SteamForge
