#pragma once

namespace SteamForge {

// RSAES-PKCS1-v1_5 with a public key given as big-endian modulus and exponent, as Steam publishes it for passwords.
[[nodiscard]] std::optional<QByteArray> EncryptRsaPkcs1V15(QByteArrayView message, QByteArrayView modulus, QByteArrayView publicExponent);

} // namespace SteamForge
