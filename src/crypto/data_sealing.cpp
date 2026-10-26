#include "crypto/data_sealing.h"

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace SteamForge {

namespace {
	constexpr int NONCE_LENGTH = 12;
	constexpr int TAG_LENGTH = 16;

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
		return length >= 0 && length <= std::numeric_limits<int>::max() - TAG_LENGTH;
	}

	// Sets the cipher, the nonce length, the key and the nonce, then feeds the associated data.
	bool Prepare(EVP_CIPHER_CTX& context, bool isEncrypting, const DataKey& key, QByteArrayView nonce, QByteArrayView associatedData) {
		const int initialized = isEncrypting ? EVP_EncryptInit_ex(&context, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr) : EVP_DecryptInit_ex(&context, EVP_chacha20_poly1305(), nullptr, nullptr, nullptr);
		if (initialized != 1 || EVP_CIPHER_CTX_ctrl(&context, EVP_CTRL_AEAD_SET_IVLEN, NONCE_LENGTH, nullptr) != 1) {
			return false;
		}
		const int keyed = isEncrypting ? EVP_EncryptInit_ex(&context, nullptr, nullptr, ByteData(key.Bytes()), ByteData(nonce)) : EVP_DecryptInit_ex(&context, nullptr, nullptr, ByteData(key.Bytes()), ByteData(nonce));
		if (keyed != 1) {
			return false;
		}
		int associatedLength = 0;
		const int associated = isEncrypting ? EVP_EncryptUpdate(&context, nullptr, &associatedLength, ByteData(associatedData), static_cast<int>(associatedData.size())) : EVP_DecryptUpdate(&context, nullptr, &associatedLength, ByteData(associatedData), static_cast<int>(associatedData.size()));
		return associated == 1;
	}
} // namespace

std::optional<DataKey> DataKey::Generate() {
	DataKey key;
	if (RAND_bytes(key.bytes.data(), static_cast<int>(key.bytes.size())) != 1) {
		return std::nullopt;
	}
	return key;
}

std::optional<DataKey> DataKey::FromBytes(QByteArrayView keyBytes) {
	if (keyBytes.size() != DATA_KEY_LENGTH) {
		return std::nullopt;
	}
	DataKey key;
	std::ranges::copy(keyBytes, key.bytes.begin());
	return key;
}

DataKey::~DataKey() {
	OPENSSL_cleanse(bytes.data(), bytes.size());
}

QByteArrayView DataKey::Bytes() const {
	return {reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())};
}

std::optional<SealedData> Seal(const DataKey& key, QByteArrayView plaintext, QByteArrayView associatedData) {
	if (!FitsInInt(plaintext.size()) || !FitsInInt(associatedData.size())) {
		return std::nullopt;
	}
	SealedData sealed{
		.nonce = QByteArray(NONCE_LENGTH, Qt::Uninitialized),
		.ciphertext = QByteArray(plaintext.size() + TAG_LENGTH, Qt::Uninitialized),
	};
	// Random nonces stay unique far beyond the few thousand files a data key ever seals.
	if (RAND_bytes(WritableByteData(sealed.nonce), NONCE_LENGTH) != 1) {
		return std::nullopt;
	}
	const CipherContext context(EVP_CIPHER_CTX_new());
	if (!context || !Prepare(*context, true, key, sealed.nonce, associatedData)) {
		return std::nullopt;
	}
	int encryptedLength = 0;
	if (EVP_EncryptUpdate(context.get(), WritableByteData(sealed.ciphertext), &encryptedLength, ByteData(plaintext), static_cast<int>(plaintext.size())) != 1) {
		return std::nullopt;
	}
	int finalLength = 0;
	if (EVP_EncryptFinal_ex(context.get(), WritableByteData(sealed.ciphertext) + encryptedLength, &finalLength) != 1) {
		return std::nullopt;
	}
	if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_GET_TAG, TAG_LENGTH, WritableByteData(sealed.ciphertext) + encryptedLength + finalLength) != 1) {
		return std::nullopt;
	}
	sealed.ciphertext.resize(encryptedLength + finalLength + TAG_LENGTH);
	return sealed;
}

std::optional<QByteArray> Unseal(const DataKey& key, const SealedData& sealed, QByteArrayView associatedData) {
	if (sealed.nonce.size() != NONCE_LENGTH || sealed.ciphertext.size() < TAG_LENGTH || !FitsInInt(sealed.ciphertext.size()) || !FitsInInt(associatedData.size())) {
		return std::nullopt;
	}
	const qsizetype encryptedLength = sealed.ciphertext.size() - TAG_LENGTH;
	const CipherContext context(EVP_CIPHER_CTX_new());
	if (!context || !Prepare(*context, false, key, sealed.nonce, associatedData)) {
		return std::nullopt;
	}
	QByteArray plaintext(encryptedLength, Qt::Uninitialized);
	int decryptedLength = 0;
	if (EVP_DecryptUpdate(context.get(), WritableByteData(plaintext), &decryptedLength, ByteData(sealed.ciphertext), static_cast<int>(encryptedLength)) != 1) {
		return std::nullopt;
	}
	QByteArray tag = sealed.ciphertext.sliced(encryptedLength);
	if (EVP_CIPHER_CTX_ctrl(context.get(), EVP_CTRL_AEAD_SET_TAG, TAG_LENGTH, WritableByteData(tag)) != 1) {
		return std::nullopt;
	}
	int finalLength = 0;
	if (EVP_DecryptFinal_ex(context.get(), WritableByteData(plaintext) + decryptedLength, &finalLength) != 1) {
		OPENSSL_cleanse(plaintext.data(), static_cast<std::size_t>(plaintext.size()));
		return std::nullopt;
	}
	plaintext.resize(decryptedLength + finalLength);
	return plaintext;
}

} // namespace SteamForge
