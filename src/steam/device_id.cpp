#include "steam/device_id.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView ANDROID_DEVICE_PREFIX{"android:"};
	constexpr char GROUP_SEPARATOR = '-';
	// UUID layout 8-4-4-4-12 over the first 32 hex digits of the SHA-1 digest.
	constexpr std::array<qsizetype, 5> GROUP_LENGTHS{8, 4, 4, 4, 12};
} // namespace

QString DeriveNodeSteamTotpDeviceId(quint64 steamId) {
	const QByteArray hexDigest = QCryptographicHash::hash(QByteArray::number(steamId), QCryptographicHash::Sha1).toHex();
	QByteArray deviceId;
	qsizetype groupStart = 0;
	for (const qsizetype groupLength : GROUP_LENGTHS) {
		if (groupStart > 0) {
			deviceId.append(GROUP_SEPARATOR);
		}
		deviceId.append(hexDigest.mid(groupStart, groupLength));
		groupStart += groupLength;
	}
	return ANDROID_DEVICE_PREFIX + QString::fromLatin1(deviceId);
}

QString GenerateDeviceId() {
	return ANDROID_DEVICE_PREFIX + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace SteamForge
