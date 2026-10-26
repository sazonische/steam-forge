#include "crypto/rsa_encryption.h"

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/param_build.h>
#include <openssl/rsa.h>

namespace SteamForge {

namespace {
	constexpr const char* RSA_ALGORITHM = "RSA";

	struct BigNumberDeleter {
		void operator()(BIGNUM* number) const {
			BN_free(number);
		}
	};
	struct ParameterBuilderDeleter {
		void operator()(OSSL_PARAM_BLD* builder) const {
			OSSL_PARAM_BLD_free(builder);
		}
	};
	struct ParametersDeleter {
		void operator()(OSSL_PARAM* parameters) const {
			OSSL_PARAM_free(parameters);
		}
	};
	struct KeyContextDeleter {
		void operator()(EVP_PKEY_CTX* context) const {
			EVP_PKEY_CTX_free(context);
		}
	};
	struct KeyDeleter {
		void operator()(EVP_PKEY* key) const {
			EVP_PKEY_free(key);
		}
	};
	using BigNumber = std::unique_ptr<BIGNUM, BigNumberDeleter>;
	using ParameterBuilder = std::unique_ptr<OSSL_PARAM_BLD, ParameterBuilderDeleter>;
	using Parameters = std::unique_ptr<OSSL_PARAM, ParametersDeleter>;
	using KeyContext = std::unique_ptr<EVP_PKEY_CTX, KeyContextDeleter>;
	using Key = std::unique_ptr<EVP_PKEY, KeyDeleter>;

	const unsigned char* ByteData(QByteArrayView bytes) {
		return reinterpret_cast<const unsigned char*>(bytes.data());
	}

	bool FitsInInt(qsizetype length) {
		return length > 0 && length <= std::numeric_limits<int>::max();
	}

	Key PublicKey(QByteArrayView modulus, QByteArrayView publicExponent) {
		const BigNumber modulusNumber(BN_bin2bn(ByteData(modulus), static_cast<int>(modulus.size()), nullptr));
		const BigNumber exponentNumber(BN_bin2bn(ByteData(publicExponent), static_cast<int>(publicExponent.size()), nullptr));
		const ParameterBuilder builder(OSSL_PARAM_BLD_new());
		if (!modulusNumber || !exponentNumber || !builder) {
			return nullptr;
		}
		if (OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_N, modulusNumber.get()) != 1 || OSSL_PARAM_BLD_push_BN(builder.get(), OSSL_PKEY_PARAM_RSA_E, exponentNumber.get()) != 1) {
			return nullptr;
		}
		const Parameters parameters(OSSL_PARAM_BLD_to_param(builder.get()));
		const KeyContext context(EVP_PKEY_CTX_new_from_name(nullptr, RSA_ALGORITHM, nullptr));
		if (!parameters || !context || EVP_PKEY_fromdata_init(context.get()) != 1) {
			return nullptr;
		}
		EVP_PKEY* key = nullptr;
		if (EVP_PKEY_fromdata(context.get(), &key, EVP_PKEY_PUBLIC_KEY, parameters.get()) != 1) {
			return nullptr;
		}
		return Key(key);
	}
} // namespace

std::optional<QByteArray> EncryptRsaPkcs1V15(QByteArrayView message, QByteArrayView modulus, QByteArrayView publicExponent) {
	if (!FitsInInt(message.size()) || !FitsInInt(modulus.size()) || !FitsInInt(publicExponent.size())) {
		return std::nullopt;
	}
	const Key key = PublicKey(modulus, publicExponent);
	if (!key) {
		return std::nullopt;
	}
	const KeyContext context(EVP_PKEY_CTX_new_from_pkey(nullptr, key.get(), nullptr));
	if (!context || EVP_PKEY_encrypt_init(context.get()) != 1 || EVP_PKEY_CTX_set_rsa_padding(context.get(), RSA_PKCS1_PADDING) != 1) {
		return std::nullopt;
	}

	std::size_t ciphertextLength = 0;
	if (EVP_PKEY_encrypt(context.get(), nullptr, &ciphertextLength, ByteData(message), static_cast<std::size_t>(message.size())) != 1) {
		return std::nullopt;
	}
	QByteArray ciphertext(static_cast<qsizetype>(ciphertextLength), Qt::Uninitialized);
	if (EVP_PKEY_encrypt(context.get(), reinterpret_cast<unsigned char*>(ciphertext.data()), &ciphertextLength, ByteData(message), static_cast<std::size_t>(message.size())) != 1) {
		return std::nullopt;
	}
	ciphertext.resize(static_cast<qsizetype>(ciphertextLength));
	return ciphertext;
}

} // namespace SteamForge
