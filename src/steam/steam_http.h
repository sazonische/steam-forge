#pragma once

#include "steam/web_api_transport.h"

namespace SteamForge {

// Headers the Steam mobile application 3.10.9 sends; extra cookies are appended to its own.
void ApplyMobileClientHeaders(QNetworkRequest& networkRequest, const QByteArray& extraCookies = {});

// The Steam name of the interface language; like the Steam mobile application, texts are asked for in it.
[[nodiscard]] QLatin1StringView InterfaceSteamLanguage();

// Percent-encodes every name and value, so '+' of Base64 is not read back as a space.
[[nodiscard]] QByteArray EncodeParameters(const RequestParameters& parameters);

// Limits the response size, aborts the reply when the context is destroyed and hands the finished reply to the handler.
void WatchReply(QNetworkReply* reply, QObject& context, std::function<void(QNetworkReply&)> finishedHandler);

// The failure of a finished reply before its body is read: size limit, timeout, network or a status other than 2xx.
[[nodiscard]] std::optional<WebApiError> ReplyFailure(QNetworkReply& reply);

} // namespace SteamForge
