#include "cloud/google_oauth.h"

namespace {

SteamForge::GoogleClient TestClient() {
	return SteamForge::GoogleClient{.clientId = QStringLiteral("client-id.apps.googleusercontent.com"), .clientSecret = QStringLiteral("client-secret")};
}

QString UrlSafeBase64(const QByteArray& bytes) {
	return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

QByteArray TokenResponse(const QJsonObject& fields) {
	return QJsonDocument(fields).toJson(QJsonDocument::Compact);
}

QString IdTokenWithEmail(const QString& email) {
	const QByteArray header = QJsonDocument(QJsonObject{{QStringLiteral("alg"), QStringLiteral("RS256")}}).toJson(QJsonDocument::Compact);
	const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("email"), email}}).toJson(QJsonDocument::Compact);
	return UrlSafeBase64(header) + u'.' + UrlSafeBase64(payload) + QStringLiteral(".signature");
}

QUrlQuery FormOf(const SteamForge::GoogleRequest& request) {
	return QUrlQuery(QString::fromLatin1(request.body));
}

} // namespace

class GoogleOAuthTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void DerivesTheChallengeFromTheVerifier() {
		const SteamForge::ProofKey proofKey = SteamForge::ProofKeyFromVerifier(QStringLiteral("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"));
		QCOMPARE(proofKey.challenge, QStringLiteral("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
	}

	void GeneratesAVerifierOfTheRequiredLength() {
		const std::optional<SteamForge::ProofKey> proofKey = SteamForge::GenerateProofKey();
		QVERIFY(proofKey.has_value());
		QCOMPARE(proofKey->verifier.size(), 43);
		QVERIFY(QRegularExpression(QStringLiteral("^[A-Za-z0-9_-]+$")).match(proofKey->verifier).hasMatch());
		QCOMPARE(proofKey->challenge, SteamForge::ProofKeyFromVerifier(proofKey->verifier).challenge);
		const std::optional<SteamForge::ProofKey> otherKey = SteamForge::GenerateProofKey();
		QVERIFY(otherKey.has_value());
		QVERIFY(otherKey->verifier != proofKey->verifier);
	}

	void AsksOnlyForTheApplicationFolderAndTheAddress() {
		const SteamForge::ProofKey proofKey = SteamForge::ProofKeyFromVerifier(QStringLiteral("verifier"));
		const QUrl url = SteamForge::AuthorizationUrl(SteamForge::DefaultGoogleEndpoints(), TestClient(), QStringLiteral("http://127.0.0.1:5000"), proofKey, QStringLiteral("state-value"));
		QCOMPARE(url.host(), QStringLiteral("accounts.google.com"));
		const QUrlQuery query(url);
		QCOMPARE(query.queryItemValue(QStringLiteral("client_id")), TestClient().clientId);
		QCOMPARE(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded), QStringLiteral("http://127.0.0.1:5000"));
		QCOMPARE(query.queryItemValue(QStringLiteral("response_type")), QStringLiteral("code"));
		QCOMPARE(query.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded), QStringLiteral("openid email https://www.googleapis.com/auth/drive.appdata"));
		QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge")), proofKey.challenge);
		QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
		QCOMPARE(query.queryItemValue(QStringLiteral("state")), QStringLiteral("state-value"));
		QVERIFY(!query.hasQueryItem(QStringLiteral("client_secret")));
		QVERIFY(!url.toString().contains(proofKey.verifier));
	}

	void ExchangesTheCodeWithTheVerifier() {
		const SteamForge::ProofKey proofKey = SteamForge::ProofKeyFromVerifier(QStringLiteral("verifier+/="));
		const SteamForge::GoogleRequest request = SteamForge::CodeExchangeRequest(SteamForge::DefaultGoogleEndpoints(), TestClient(), QStringLiteral("4/0Abc"), proofKey, QStringLiteral("http://127.0.0.1:5000"));
		QCOMPARE(request.method, SteamForge::GoogleHttpMethod::POST);
		QCOMPARE(request.url.toString(), QStringLiteral("https://oauth2.googleapis.com/token"));
		QCOMPARE(request.contentType, QByteArray("application/x-www-form-urlencoded"));
		const QUrlQuery form = FormOf(request);
		QCOMPARE(form.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded), QStringLiteral("4/0Abc"));
		QCOMPARE(form.queryItemValue(QStringLiteral("code_verifier"), QUrl::FullyDecoded), QStringLiteral("verifier+/="));
		QCOMPARE(form.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("authorization_code"));
		QCOMPARE(form.queryItemValue(QStringLiteral("client_secret")), TestClient().clientSecret);
		QVERIFY(request.accessToken.isEmpty());
	}

	void LeavesOutAnEmptyClientSecret() {
		const SteamForge::GoogleClient publicClient{.clientId = QStringLiteral("client-id"), .clientSecret = {}};
		const SteamForge::GoogleRequest request = SteamForge::TokenRefreshRequest(SteamForge::DefaultGoogleEndpoints(), publicClient, QStringLiteral("refresh"));
		const QUrlQuery form = FormOf(request);
		QCOMPARE(form.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
		QCOMPARE(form.queryItemValue(QStringLiteral("refresh_token")), QStringLiteral("refresh"));
		QVERIFY(!form.hasQueryItem(QStringLiteral("client_secret")));
	}

	void ReadsTheTokensAndTheAccount() {
		const QDateTime receivedAt = QDateTime::currentDateTimeUtc();
		const QByteArray response = TokenResponse({
			{QStringLiteral("access_token"), QStringLiteral("access")},
			{QStringLiteral("refresh_token"), QStringLiteral("refresh")},
			{QStringLiteral("expires_in"), 3599},
			{QStringLiteral("scope"), QStringLiteral("openid https://www.googleapis.com/auth/userinfo.email https://www.googleapis.com/auth/drive.appdata")},
			{QStringLiteral("token_type"), QStringLiteral("Bearer")},
			{QStringLiteral("id_token"), IdTokenWithEmail(QStringLiteral("player@example.com"))},
		});
		const std::optional<SteamForge::GoogleTokens> tokens = SteamForge::ParseTokenResponse(response, receivedAt);
		QVERIFY(tokens.has_value());
		QCOMPARE(tokens->accessToken, QStringLiteral("access"));
		QCOMPARE(tokens->refreshToken, QStringLiteral("refresh"));
		QCOMPARE(tokens->accessTokenExpiresAt, receivedAt.addSecs(3599));
		QCOMPARE(tokens->email, QStringLiteral("player@example.com"));
		QVERIFY(SteamForge::GrantsDriveAccess(*tokens));
	}

	void NoticesAnUntickedDrivePermission() {
		const QByteArray response = TokenResponse({
			{QStringLiteral("access_token"), QStringLiteral("access")},
			{QStringLiteral("expires_in"), 3599},
			{QStringLiteral("scope"), QStringLiteral("openid https://www.googleapis.com/auth/userinfo.email")},
		});
		const std::optional<SteamForge::GoogleTokens> tokens = SteamForge::ParseTokenResponse(response, QDateTime::currentDateTimeUtc());
		QVERIFY(tokens.has_value());
		QVERIFY(!SteamForge::GrantsDriveAccess(*tokens));
	}

	void RefusesAnAnswerWithoutAccessToken() {
		QVERIFY(!SteamForge::ParseTokenResponse(TokenResponse({{QStringLiteral("error"), QStringLiteral("invalid_grant")}}), QDateTime::currentDateTimeUtc()).has_value());
		QVERIFY(!SteamForge::ParseTokenResponse(QByteArray("not json"), QDateTime::currentDateTimeUtc()).has_value());
	}
};

QTEST_GUILESS_MAIN(GoogleOAuthTest)

#include "google_oauth_test.moc"
