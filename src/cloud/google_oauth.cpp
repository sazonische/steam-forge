#include "cloud/google_oauth.h"

#include "steam/steam_http.h"

#include <openssl/crypto.h>
#include <openssl/rand.h>

namespace SteamForge {

namespace {
	// 32 bytes give the 43 characters RFC 7636 asks for at least.
	constexpr qsizetype PROOF_KEY_RANDOM_LENGTH = 32;
	constexpr qsizetype STATE_RANDOM_LENGTH = 16;
	constexpr QLatin1StringView SIGN_IN_SCOPES_PATTERN{"openid email %1"};
	constexpr QLatin1StringView CODE_RESPONSE_TYPE{"code"};
	constexpr QLatin1StringView CHALLENGE_METHOD{"S256"};
	constexpr QLatin1StringView AUTHORIZATION_CODE_GRANT{"authorization_code"};
	constexpr QLatin1StringView REFRESH_TOKEN_GRANT{"refresh_token"};
	constexpr QByteArrayView FORM_CONTENT_TYPE{"application/x-www-form-urlencoded"};
	constexpr QLatin1StringView ACCESS_TOKEN_FIELD{"access_token"};
	constexpr QLatin1StringView REFRESH_TOKEN_FIELD{"refresh_token"};
	constexpr QLatin1StringView EXPIRES_IN_FIELD{"expires_in"};
	constexpr QLatin1StringView SCOPE_FIELD{"scope"};
	constexpr QLatin1StringView ID_TOKEN_FIELD{"id_token"};
	constexpr QLatin1StringView EMAIL_CLAIM{"email"};
	constexpr QChar JWT_PART_SEPARATOR{u'.'};
	constexpr QByteArray::Base64Options URL_SAFE_BASE64 = QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals;

	std::optional<QString> RandomUrlSafeText(qsizetype byteCount) {
		QByteArray randomBytes(byteCount, '\0');
		if (RAND_bytes(reinterpret_cast<unsigned char*>(randomBytes.data()), static_cast<int>(randomBytes.size())) != 1) {
			return std::nullopt;
		}
		QString text = QString::fromLatin1(randomBytes.toBase64(URL_SAFE_BASE64));
		OPENSSL_cleanse(randomBytes.data(), static_cast<std::size_t>(randomBytes.size()));
		return text;
	}

	GoogleRequest FormRequest(const QUrl& url, const RequestParameters& parameters) {
		return GoogleRequest{
			.method = GoogleHttpMethod::POST,
			.url = url,
			.contentType = FORM_CONTENT_TYPE.toByteArray(),
			.body = EncodeParameters(parameters),
			.accessToken = {},
		};
	}

	RequestParameters WithClient(const GoogleClient& client, RequestParameters parameters) {
		parameters.prepend({QStringLiteral("client_id"), client.clientId});
		if (!client.clientSecret.isEmpty()) {
			parameters.append({QStringLiteral("client_secret"), client.clientSecret});
		}
		return parameters;
	}

	// The ID token comes straight from Google over TLS in the same answer, so its claims are read without verifying
	// the signature; it only names the account for the user.
	QString EmailOfIdToken(const QString& idToken) {
		const QStringList parts = idToken.split(JWT_PART_SEPARATOR);
		if (parts.size() != 3) {
			return {};
		}
		const QByteArray::FromBase64Result payload = QByteArray::fromBase64Encoding(parts.at(1).toLatin1(), URL_SAFE_BASE64 | QByteArray::AbortOnBase64DecodingErrors);
		if (!payload) {
			return {};
		}
		return QJsonDocument::fromJson(payload.decoded).object().value(EMAIL_CLAIM).toString();
	}
} // namespace

std::optional<ProofKey> GenerateProofKey() {
	const std::optional<QString> verifier = RandomUrlSafeText(PROOF_KEY_RANDOM_LENGTH);
	if (!verifier.has_value()) {
		return std::nullopt;
	}
	return ProofKeyFromVerifier(*verifier);
}

ProofKey ProofKeyFromVerifier(const QString& verifier) {
	const QByteArray digest = QCryptographicHash::hash(verifier.toLatin1(), QCryptographicHash::Sha256);
	return ProofKey{.verifier = verifier, .challenge = QString::fromLatin1(digest.toBase64(URL_SAFE_BASE64))};
}

std::optional<QString> GenerateState() {
	return RandomUrlSafeText(STATE_RANDOM_LENGTH);
}

QUrl AuthorizationUrl(const GoogleEndpoints& endpoints, const GoogleClient& client, const QString& redirectUri, const ProofKey& proofKey, const QString& state) {
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("client_id"), client.clientId);
	query.addQueryItem(QStringLiteral("redirect_uri"), redirectUri);
	query.addQueryItem(QStringLiteral("response_type"), CODE_RESPONSE_TYPE);
	query.addQueryItem(QStringLiteral("scope"), QString(SIGN_IN_SCOPES_PATTERN).arg(DRIVE_APPLICATION_DATA_SCOPE));
	query.addQueryItem(QStringLiteral("code_challenge"), proofKey.challenge);
	query.addQueryItem(QStringLiteral("code_challenge_method"), CHALLENGE_METHOD);
	query.addQueryItem(QStringLiteral("state"), state);
	QUrl url = endpoints.authorizationUrl;
	url.setQuery(query);
	return url;
}

GoogleRequest CodeExchangeRequest(const GoogleEndpoints& endpoints, const GoogleClient& client, const QString& authorizationCode, const ProofKey& proofKey, const QString& redirectUri) {
	const RequestParameters parameters{
		{QStringLiteral("code"), authorizationCode},
		{QStringLiteral("code_verifier"), proofKey.verifier},
		{QStringLiteral("grant_type"), AUTHORIZATION_CODE_GRANT},
		{QStringLiteral("redirect_uri"), redirectUri},
	};
	return FormRequest(endpoints.tokenUrl, WithClient(client, parameters));
}

GoogleRequest TokenRefreshRequest(const GoogleEndpoints& endpoints, const GoogleClient& client, const QString& refreshToken) {
	const RequestParameters parameters{
		{QStringLiteral("grant_type"), REFRESH_TOKEN_GRANT},
		{QStringLiteral("refresh_token"), refreshToken},
	};
	return FormRequest(endpoints.tokenUrl, WithClient(client, parameters));
}

GoogleRequest RevocationRequest(const GoogleEndpoints& endpoints, const QString& token) {
	return FormRequest(endpoints.revocationUrl, {{QStringLiteral("token"), token}});
}

std::optional<GoogleTokens> ParseTokenResponse(const QByteArray& responseJson, const QDateTime& receivedAt) {
	const QJsonObject response = QJsonDocument::fromJson(responseJson).object();
	const QString accessToken = response.value(ACCESS_TOKEN_FIELD).toString();
	const qint64 lifetimeSeconds = response.value(EXPIRES_IN_FIELD).toInteger();
	if (accessToken.isEmpty() || lifetimeSeconds <= 0) {
		return std::nullopt;
	}
	return GoogleTokens{
		.refreshToken = response.value(REFRESH_TOKEN_FIELD).toString(),
		.accessToken = accessToken,
		.accessTokenExpiresAt = receivedAt.addSecs(lifetimeSeconds),
		.grantedScopes = response.value(SCOPE_FIELD).toString().split(u' ', Qt::SkipEmptyParts),
		.email = EmailOfIdToken(response.value(ID_TOKEN_FIELD).toString()),
	};
}

bool GrantsDriveAccess(const GoogleTokens& tokens) {
	return tokens.grantedScopes.contains(DRIVE_APPLICATION_DATA_SCOPE);
}

} // namespace SteamForge
