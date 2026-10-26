#pragma once

namespace SteamForge {

inline constexpr qsizetype DATA_KEY_LENGTH = 32;

// The key that encrypts the data of this computer; its bytes are wiped when it is released.
class DataKey final {
public:
	// A new key from the random generator of the system.
	[[nodiscard]] static std::optional<DataKey> Generate();
	[[nodiscard]] static std::optional<DataKey> FromBytes(QByteArrayView keyBytes);

	DataKey(const DataKey&) = default;
	DataKey(DataKey&&) = default;
	DataKey& operator=(const DataKey&) = default;
	DataKey& operator=(DataKey&&) = default;
	~DataKey();

	[[nodiscard]] QByteArrayView Bytes() const;

private:
	DataKey() = default;

	std::array<unsigned char, DATA_KEY_LENGTH> bytes{};
};

struct SealedData {
	QByteArray nonce;
	// The authentication tag follows the encrypted bytes.
	QByteArray ciphertext;
};

// ChaCha20-Poly1305 with a random 96-bit nonce. The associated data names the place of the data, so sealed data
// moved to another place does not open.
[[nodiscard]] std::optional<SealedData> Seal(const DataKey& key, QByteArrayView plaintext, QByteArrayView associatedData);
// Nothing when the key, the place or a single byte of the data differ.
[[nodiscard]] std::optional<QByteArray> Unseal(const DataKey& key, const SealedData& sealed, QByteArrayView associatedData);

} // namespace SteamForge
