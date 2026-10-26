#include "crypto/rsa_encryption.h"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>

namespace {
constexpr unsigned int TEST_KEY_BITS = 2048;

struct KeyDeleter {
	void operator()(EVP_PKEY* key) const {
		EVP_PKEY_free(key);
	}
};
struct KeyContextDeleter {
	void operator()(EVP_PKEY_CTX* context) const {
		EVP_PKEY_CTX_free(context);
	}
};
struct BigNumberDeleter {
	void operator()(BIGNUM* number) const {
		BN_free(number);
	}
};

QByteArray BigEndianParameter(EVP_PKEY* key, const char* parameterName) {
	BIGNUM* number = nullptr;
	if (EVP_PKEY_get_bn_param(key, parameterName, &number) != 1) {
		return {};
	}
	const std::unique_ptr<BIGNUM, BigNumberDeleter> ownedNumber(number);
	QByteArray bytes(BN_num_bytes(number), Qt::Uninitialized);
	BN_bn2bin(number, reinterpret_cast<unsigned char*>(bytes.data()));
	return bytes;
}

QByteArray Decrypt(EVP_PKEY* key, const QByteArray& ciphertext) {
	const std::unique_ptr<EVP_PKEY_CTX, KeyContextDeleter> context(EVP_PKEY_CTX_new_from_pkey(nullptr, key, nullptr));
	if (!context || EVP_PKEY_decrypt_init(context.get()) != 1 || EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_PKCS1_PADDING) != 1) {
		return {};
	}
	std::size_t plaintextLength = 0;
	const auto* ciphertextData = reinterpret_cast<const unsigned char*>(ciphertext.constData());
	if (EVP_PKEY_decrypt(context.get(), nullptr, &plaintextLength, ciphertextData, static_cast<std::size_t>(ciphertext.size())) != 1) {
		return {};
	}
	QByteArray plaintext(static_cast<qsizetype>(plaintextLength), Qt::Uninitialized);
	if (EVP_PKEY_decrypt(context.get(), reinterpret_cast<unsigned char*>(plaintext.data()), &plaintextLength, ciphertextData, static_cast<std::size_t>(ciphertext.size())) != 1) {
		return {};
	}
	plaintext.resize(static_cast<qsizetype>(plaintextLength));
	return plaintext;
}
} // namespace

class RsaEncryptionTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void PrivateKeyDecryptsTheCiphertext() {
		const std::unique_ptr<EVP_PKEY, KeyDeleter> key(EVP_RSA_gen(TEST_KEY_BITS));
		QVERIFY(key != nullptr);
		const QByteArray modulus = BigEndianParameter(key.get(), OSSL_PKEY_PARAM_RSA_N);
		const QByteArray publicExponent = BigEndianParameter(key.get(), OSSL_PKEY_PARAM_RSA_E);
		const QByteArray message = QStringLiteral("synthetic пароль").toUtf8();

		const std::optional<QByteArray> ciphertext = SteamForge::EncryptRsaPkcs1V15(message, modulus, publicExponent);
		QVERIFY(ciphertext.has_value());
		QCOMPARE(ciphertext->size(), modulus.size());
		QCOMPARE(Decrypt(key.get(), *ciphertext), message);
	}

	void PaddingIsRandomized() {
		const std::unique_ptr<EVP_PKEY, KeyDeleter> key(EVP_RSA_gen(TEST_KEY_BITS));
		const QByteArray modulus = BigEndianParameter(key.get(), OSSL_PKEY_PARAM_RSA_N);
		const QByteArray publicExponent = BigEndianParameter(key.get(), OSSL_PKEY_PARAM_RSA_E);
		const QByteArray message("same message");
		QVERIFY(SteamForge::EncryptRsaPkcs1V15(message, modulus, publicExponent) != SteamForge::EncryptRsaPkcs1V15(message, modulus, publicExponent));
	}

	void RejectsMissingKeyParts() {
		QVERIFY(!SteamForge::EncryptRsaPkcs1V15("message", {}, QByteArray::fromHex("010001")).has_value());
		QVERIFY(!SteamForge::EncryptRsaPkcs1V15({}, QByteArray(256, '\x7f'), QByteArray::fromHex("010001")).has_value());
	}
};

QTEST_GUILESS_MAIN(RsaEncryptionTest)

#include "rsa_encryption_test.moc"
