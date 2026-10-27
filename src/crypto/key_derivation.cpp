#include "crypto/key_derivation.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>

namespace SteamForge {

namespace {
	// RFC 5869 allows at most 255 blocks of the hash length.
	constexpr qsizetype MAX_DERIVED_LENGTH = qsizetype{255} * 32;

	struct KeyContextDeleter {
		void operator()(EVP_PKEY_CTX* context) const {
			EVP_PKEY_CTX_free(context);
		}
	};
	using KeyContext = std::unique_ptr<EVP_PKEY_CTX, KeyContextDeleter>;

	const unsigned char* ByteData(QByteArrayView bytes) {
		return reinterpret_cast<const unsigned char*>(bytes.data());
	}

	bool FitsInInt(QByteArrayView bytes) {
		return bytes.size() <= std::numeric_limits<int>::max();
	}
} // namespace

std::optional<QByteArray> DeriveKeyBytes(QByteArrayView keyMaterial, QByteArrayView salt, QByteArrayView info, qsizetype length) {
	if (length <= 0 || length > MAX_DERIVED_LENGTH || keyMaterial.isEmpty() || !FitsInInt(keyMaterial) || !FitsInInt(salt) || !FitsInInt(info)) {
		return std::nullopt;
	}
	const KeyContext context(EVP_PKEY_CTX_new_id(EVP_PKEY_HKDF, nullptr));
	if (context == nullptr || EVP_PKEY_derive_init(context.get()) != 1 || EVP_PKEY_CTX_set_hkdf_md(context.get(), EVP_sha256()) != 1) {
		return std::nullopt;
	}
	// OpenSSL refuses empty buffers; an absent salt is the zero salt of RFC 5869, an absent info is empty.
	if (!salt.isEmpty() && EVP_PKEY_CTX_set1_hkdf_salt(context.get(), ByteData(salt), static_cast<int>(salt.size())) != 1) {
		return std::nullopt;
	}
	if (EVP_PKEY_CTX_set1_hkdf_key(context.get(), ByteData(keyMaterial), static_cast<int>(keyMaterial.size())) != 1) {
		return std::nullopt;
	}
	if (!info.isEmpty() && EVP_PKEY_CTX_add1_hkdf_info(context.get(), ByteData(info), static_cast<int>(info.size())) != 1) {
		return std::nullopt;
	}
	QByteArray derived(length, '\0');
	auto derivedLength = static_cast<std::size_t>(length);
	if (EVP_PKEY_derive(context.get(), reinterpret_cast<unsigned char*>(derived.data()), &derivedLength) != 1 || std::cmp_not_equal(derivedLength, length)) {
		OPENSSL_cleanse(derived.data(), static_cast<std::size_t>(derived.size()));
		return std::nullopt;
	}
	return derived;
}

} // namespace SteamForge
