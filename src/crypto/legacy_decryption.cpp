#include "crypto/legacy_decryption.h"

#include <openssl/evp.h>

namespace SteamForge {

namespace {
	constexpr qsizetype AES_256_KEY_LENGTH = 32;
	constexpr qsizetype AES_BLOCK_LENGTH = 16;

	struct CipherContextDeleter {
		void operator()(EVP_CIPHER_CTX* context) const {
			EVP_CIPHER_CTX_free(context);
		}
	};
	using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, CipherContextDeleter>;

	const unsigned char* ByteData(QByteArrayView bytes) {
		return reinterpret_cast<const unsigned char*>(bytes.data());
	}

	unsigned char* WritableByteData(QByteArray& bytes) {
		return reinterpret_cast<unsigned char*>(bytes.data());
	}

	bool FitsInInt(qsizetype length) {
		return length >= 0 && length <= std::numeric_limits<int>::max();
	}
} // namespace

std::optional<QByteArray> DeriveKeyPbkdf2HmacSha1(QByteArrayView password, QByteArrayView salt, int iterationCount, int keyLength) {
	if (!FitsInInt(password.size()) || !FitsInInt(salt.size()) || iterationCount <= 0 || keyLength <= 0) {
		return std::nullopt;
	}
	QByteArray key(keyLength, Qt::Uninitialized);
	const int derivationResult = PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()), ByteData(salt), static_cast<int>(salt.size()), iterationCount, EVP_sha1(), keyLength, WritableByteData(key));
	if (derivationResult != 1) {
		return std::nullopt;
	}
	return key;
}

std::optional<QByteArray> DecryptAes256CbcPkcs7(QByteArrayView ciphertext, QByteArrayView key, QByteArrayView initializationVector) {
	if (key.size() != AES_256_KEY_LENGTH || initializationVector.size() != AES_BLOCK_LENGTH) {
		return std::nullopt;
	}
	if (ciphertext.isEmpty() || ciphertext.size() % AES_BLOCK_LENGTH != 0 || !FitsInInt(ciphertext.size())) {
		return std::nullopt;
	}

	const CipherContext context(EVP_CIPHER_CTX_new());
	if (!context || EVP_DecryptInit_ex(context.get(), EVP_aes_256_cbc(), nullptr, ByteData(key), ByteData(initializationVector)) != 1) {
		return std::nullopt;
	}

	// A wrong key almost always fails the PKCS#7 padding check in EVP_DecryptFinal_ex.
	QByteArray plaintext(ciphertext.size() + AES_BLOCK_LENGTH, Qt::Uninitialized);
	int updateLength = 0;
	if (EVP_DecryptUpdate(context.get(), WritableByteData(plaintext), &updateLength, ByteData(ciphertext), static_cast<int>(ciphertext.size())) != 1) {
		return std::nullopt;
	}
	int finalLength = 0;
	if (EVP_DecryptFinal_ex(context.get(), WritableByteData(plaintext) + updateLength, &finalLength) != 1) {
		return std::nullopt;
	}
	plaintext.resize(updateLength + finalLength);
	return plaintext;
}

} // namespace SteamForge
