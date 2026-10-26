#include "support/synthetic_tokens.h"

QString SyntheticJwt(quint64 steamId, const QStringList& audiences, qint64 expiresAtSeconds) {
	const QJsonObject payload{
		{QStringLiteral("sub"), QString::number(steamId)},
		{QStringLiteral("aud"), QJsonArray::fromStringList(audiences)},
		{QStringLiteral("exp"), expiresAtSeconds},
	};
	const auto base64Url = [](const QByteArray& bytes) {
		return bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	};
	return QString::fromLatin1(base64Url(R"({"typ":"JWT","alg":"EdDSA"})") + '.' + base64Url(QJsonDocument(payload).toJson(QJsonDocument::Compact)) + '.' + base64Url("synthetic signature"));
}
