#include "steam/jwt_claims.h"

namespace {
QString Token(const QByteArray& payloadJson) {
	const QByteArray header = QByteArray(R"({"typ":"JWT","alg":"EdDSA"})").toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	const QByteArray payload = payloadJson.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	return QString::fromLatin1(header + '.' + payload + ".c2lnbmF0dXJl");
}
} // namespace

class JwtClaimsTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void ReadsSubjectAudiencesAndExpiration() {
		const std::optional<SteamForge::JwtClaims> claims = SteamForge::ReadJwtClaims(Token(R"({"iss":"steam","sub":"76561198000000001","aud":["mobile","web"],"exp":1800000000})"));
		QVERIFY(claims.has_value());
		QCOMPARE(claims->steamId, quint64{76561198000000001});
		QVERIFY(SteamForge::HasAudience(*claims, QLatin1StringView("mobile")));
		QVERIFY(!SteamForge::HasAudience(*claims, QLatin1StringView("derive")));
		QCOMPARE(claims->expiresAt, std::optional(QDateTime::fromSecsSinceEpoch(1800000000, QTimeZone::UTC)));
	}

	void AcceptsSingleAudienceString() {
		const std::optional<SteamForge::JwtClaims> claims = SteamForge::ReadJwtClaims(Token(R"({"sub":"76561198000000001","aud":"mobile"})"));
		QVERIFY(claims.has_value());
		QVERIFY(SteamForge::HasAudience(*claims, QLatin1StringView("mobile")));
		QVERIFY(!claims->expiresAt.has_value());
	}

	void RejectsMalformedTokens_data() {
		QTest::addColumn<QString>("token");
		QTest::newRow("empty") << QString();
		QTest::newRow("two segments") << QStringLiteral("abc.def");
		QTest::newRow("payload not base64") << QStringLiteral("abc.%%%.def");
		QTest::newRow("no subject") << Token(R"({"aud":["mobile"]})");
		QTest::newRow("subject not a number") << Token(R"({"sub":"someone"})");
	}

	void RejectsMalformedTokens() {
		QFETCH(QString, token);
		QVERIFY(!SteamForge::ReadJwtClaims(token).has_value());
	}

	void ExpiresOnlyWithAKnownPastExpiration() {
		const QDateTime moment = QDateTime::fromSecsSinceEpoch(1800000000, QTimeZone::UTC);
		QVERIFY(SteamForge::HasExpired(Token(R"({"sub":"76561198000000001","exp":1800000000})"), moment));
		QVERIFY(!SteamForge::HasExpired(Token(R"({"sub":"76561198000000001","exp":1800000001})"), moment));
		QVERIFY(!SteamForge::HasExpired(Token(R"({"sub":"76561198000000001"})"), moment));
		QVERIFY(!SteamForge::HasExpired(QStringLiteral("not a token"), moment));
	}
};

QTEST_GUILESS_MAIN(JwtClaimsTest)

#include "jwt_claims_test.moc"
