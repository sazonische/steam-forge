#include "steam/qr_challenge.h"

#include <ZXing/ReadBarcode.h>

namespace SteamForge {

namespace {
	// Same form node-steam-session LoginApprover accepts; the query part is optional.
	constexpr QLatin1StringView CHALLENGE_URL_PATTERN{R"(^https?://s\.team/q/(\d+)/(\d+)(\?.*)?$)"};
	constexpr int VERSION_GROUP = 1;
	constexpr int CLIENT_ID_GROUP = 2;
	constexpr qsizetype SIGNED_DATA_LENGTH = 2 + 8 + 8;
	constexpr qsizetype CLIENT_ID_OFFSET = 2;
	constexpr qsizetype STEAM_ID_OFFSET = 10;
} // namespace

std::optional<QrChallenge> ParseQrChallengeUrl(QStringView url) {
	const QRegularExpression challengePattern(CHALLENGE_URL_PATTERN);
	const QRegularExpressionMatch match = challengePattern.matchView(url.trimmed());
	if (!match.hasMatch()) {
		return std::nullopt;
	}
	bool isVersionValid = false;
	bool isClientIdValid = false;
	const int version = match.capturedView(VERSION_GROUP).toInt(&isVersionValid);
	const quint64 clientId = match.capturedView(CLIENT_ID_GROUP).toULongLong(&isClientIdValid);
	if (!isVersionValid || !isClientIdValid || !std::in_range<quint16>(version)) {
		return std::nullopt;
	}
	return QrChallenge{
		.version = version,
		.clientId = clientId,
	};
}

QByteArray SignQrChallenge(const QrChallenge& challenge, quint64 steamId, QByteArrayView sharedSecret) {
	QByteArray signedData(SIGNED_DATA_LENGTH, Qt::Uninitialized);
	qToLittleEndian(static_cast<quint16>(challenge.version), signedData.data());
	qToLittleEndian(challenge.clientId, signedData.data() + CLIENT_ID_OFFSET);
	qToLittleEndian(steamId, signedData.data() + STEAM_ID_OFFSET);
	return QMessageAuthenticationCode::hash(signedData, sharedSecret, QCryptographicHash::Sha256);
}

std::vector<QrChallenge> FindQrChallenges(const QImage& image) {
	const QImage grayscaleImage = image.convertToFormat(QImage::Format_Grayscale8);
	if (grayscaleImage.isNull()) {
		return {};
	}
	ZXing::ReaderOptions readerOptions;
	readerOptions.setFormats(ZXing::BarcodeFormat::QRCode);
	readerOptions.setTryHarder(true);
	const ZXing::ImageView imageView(grayscaleImage.constBits(), grayscaleImage.width(), grayscaleImage.height(), ZXing::ImageFormat::Lum, static_cast<int>(grayscaleImage.bytesPerLine()));

	std::vector<QrChallenge> challenges;
	for (const auto& barcode : ZXing::ReadBarcodes(imageView, readerOptions)) {
		if (!barcode.isValid()) {
			continue;
		}
		const std::optional<QrChallenge> challenge = ParseQrChallengeUrl(QString::fromStdString(barcode.text()));
		if (challenge.has_value() && std::ranges::find(challenges, *challenge) == challenges.end()) {
			challenges.push_back(*challenge);
		}
	}
	return challenges;
}

} // namespace SteamForge
