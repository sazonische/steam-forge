#include "common/encoding.h"

namespace SteamForge {

namespace {
	constexpr int BASE32_BITS_PER_CHARACTER = 5;
	constexpr int BITS_PER_BYTE = 8;
	constexpr quint32 BYTE_MASK = 0xFF;
	constexpr quint32 BASE32_FIRST_DIGIT_VALUE = 26;
	constexpr quint32 BASE32_DIGIT_MASK = 0x1F;
	constexpr QLatin1StringView BASE32_ALPHABET{"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"};
} // namespace

std::optional<QByteArray> DecodeBase64(QStringView encoded) {
	const QByteArray::FromBase64Result decoded = QByteArray::fromBase64Encoding(encoded.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
	// toLatin1() turns non-Latin-1 characters into '?', which the strict decoder rejects.
	if (!decoded) {
		return std::nullopt;
	}
	return decoded.decoded;
}

std::optional<QByteArray> DecodeBase32(QStringView encoded) {
	QByteArray decoded;
	quint32 bitBuffer = 0;
	int bufferedBitCount = 0;
	for (const QChar character : encoded) {
		if (character == u'=' || character.isSpace()) {
			continue;
		}
		const char16_t upperCharacter = character.toUpper().unicode();
		quint32 value = 0;
		if (upperCharacter >= u'A' && upperCharacter <= u'Z') {
			value = static_cast<quint32>(upperCharacter - u'A');
		} else if (upperCharacter >= u'2' && upperCharacter <= u'7') {
			value = static_cast<quint32>(upperCharacter - u'2') + BASE32_FIRST_DIGIT_VALUE;
		} else {
			return std::nullopt;
		}
		bitBuffer = (bitBuffer << BASE32_BITS_PER_CHARACTER) | value;
		bufferedBitCount += BASE32_BITS_PER_CHARACTER;
		if (bufferedBitCount >= BITS_PER_BYTE) {
			bufferedBitCount -= BITS_PER_BYTE;
			decoded.append(static_cast<char>((bitBuffer >> bufferedBitCount) & BYTE_MASK));
		}
	}
	return decoded;
}

QString EncodeBase32(const QByteArray& bytes) {
	QString encoded;
	quint32 bitBuffer = 0;
	int bufferedBitCount = 0;
	for (const char byte : bytes) {
		bitBuffer = (bitBuffer << BITS_PER_BYTE) | (static_cast<quint32>(static_cast<unsigned char>(byte)));
		bufferedBitCount += BITS_PER_BYTE;
		while (bufferedBitCount >= BASE32_BITS_PER_CHARACTER) {
			bufferedBitCount -= BASE32_BITS_PER_CHARACTER;
			encoded.append(BASE32_ALPHABET.at((bitBuffer >> bufferedBitCount) & BASE32_DIGIT_MASK));
		}
		bitBuffer &= (1U << bufferedBitCount) - 1U;
	}
	if (bufferedBitCount > 0) {
		encoded.append(BASE32_ALPHABET.at((bitBuffer << (BASE32_BITS_PER_CHARACTER - bufferedBitCount)) & BASE32_DIGIT_MASK));
	}
	return encoded;
}

} // namespace SteamForge
