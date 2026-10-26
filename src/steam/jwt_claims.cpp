#include "steam/jwt_claims.h"

namespace SteamForge {

namespace {
	constexpr char16_t SEGMENT_SEPARATOR = u'.';
	constexpr qsizetype JWT_SEGMENT_COUNT = 3;
	constexpr qsizetype PAYLOAD_SEGMENT = 1;
	constexpr QLatin1StringView SUBJECT_CLAIM{"sub"};
	constexpr QLatin1StringView AUDIENCE_CLAIM{"aud"};
	constexpr QLatin1StringView EXPIRATION_CLAIM{"exp"};
} // namespace

std::optional<JwtClaims> ReadJwtClaims(QStringView token) {
	const QList<QStringView> segments = token.split(SEGMENT_SEPARATOR);
	if (segments.size() != JWT_SEGMENT_COUNT) {
		return std::nullopt;
	}
	const QByteArray::FromBase64Result payload = QByteArray::fromBase64Encoding(segments.at(PAYLOAD_SEGMENT).toLatin1(), QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors);
	if (!payload) {
		return std::nullopt;
	}
	const QJsonObject claimsObject = QJsonDocument::fromJson(payload.decoded).object();
	bool isSubjectValid = false;
	const quint64 steamId = claimsObject.value(SUBJECT_CLAIM).toString().toULongLong(&isSubjectValid);
	if (!isSubjectValid) {
		return std::nullopt;
	}

	JwtClaims claims;
	claims.steamId = steamId;
	const QJsonValue audience = claimsObject.value(AUDIENCE_CLAIM);
	if (audience.isString()) {
		claims.audiences.append(audience.toString());
	}
	for (const QJsonValue audienceEntry : audience.toArray()) {
		claims.audiences.append(audienceEntry.toString());
	}
	const QJsonValue expiration = claimsObject.value(EXPIRATION_CLAIM);
	if (expiration.isDouble()) {
		claims.expiresAt = QDateTime::fromSecsSinceEpoch(expiration.toInteger(), QTimeZone::UTC);
	}
	return claims;
}

bool HasAudience(const JwtClaims& claims, QLatin1StringView audience) {
	return claims.audiences.contains(audience);
}

bool HasExpired(QStringView token, const QDateTime& moment) {
	const std::optional<JwtClaims> claims = ReadJwtClaims(token);
	return claims.has_value() && claims->expiresAt.has_value() && *claims->expiresAt <= moment;
}

} // namespace SteamForge
