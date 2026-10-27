#pragma once

// Ordinary players with official avatars of the Steam avatar gallery, loaded from the Steam CDN.
struct DemoAccount {
	quint64 steamId = 0;
	QString accountName;
	QString nickname;
	QLatin1StringView avatarHash;
};

inline QByteArray DemoProfileXml(const DemoAccount& account) {
	const QString avatarUrl = QStringLiteral("https://avatars.fastly.steamstatic.com/%1_full.jpg").arg(account.avatarHash);
	return QStringLiteral(R"(<?xml version="1.0" encoding="UTF-8"?><profile><steamID64>%1</steamID64><steamID><![CDATA[%2]]></steamID><avatarFull><![CDATA[%3]]></avatarFull></profile>)").arg(account.steamId).arg(account.nickname, avatarUrl).toUtf8();
}
