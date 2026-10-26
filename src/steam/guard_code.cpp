#include "steam/guard_code.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView GUARD_CODE_ALPHABET{"23456789BCDFGHJKMNPQRTVWXY"};
	constexpr auto GUARD_CODE_ALPHABET_SIZE = static_cast<quint32>(GUARD_CODE_ALPHABET.size());
	constexpr int GUARD_CODE_LENGTH = 5;
	constexpr int TIME_STEP_BYTE_COUNT = 8;
	// RFC 4226 dynamic truncation.
	constexpr unsigned char TRUNCATION_OFFSET_MASK = 0x0F;
	constexpr quint32 TRUNCATED_VALUE_MASK = 0x7FFFFFFF;
} // namespace

QString GenerateGuardCode(QByteArrayView sharedSecret, std::chrono::sys_seconds steamTime) {
	const auto timeStep = static_cast<quint64>(steamTime.time_since_epoch() / GUARD_CODE_PERIOD);
	std::array<char, TIME_STEP_BYTE_COUNT> timeStepBytes{};
	qToBigEndian(timeStep, timeStepBytes.data());

	const QByteArray digest = QMessageAuthenticationCode::hash(QByteArrayView(timeStepBytes), sharedSecret, QCryptographicHash::Sha1);
	const int offset = static_cast<unsigned char>(digest.back()) & TRUNCATION_OFFSET_MASK;
	quint32 truncatedValue = qFromBigEndian<quint32>(digest.constData() + offset) & TRUNCATED_VALUE_MASK;

	QString code;
	code.reserve(GUARD_CODE_LENGTH);
	for (int i = 0; i < GUARD_CODE_LENGTH; ++i) {
		code.append(GUARD_CODE_ALPHABET.at(truncatedValue % GUARD_CODE_ALPHABET_SIZE));
		truncatedValue /= GUARD_CODE_ALPHABET_SIZE;
	}
	return code;
}

} // namespace SteamForge
