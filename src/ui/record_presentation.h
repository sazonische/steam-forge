#pragma once

#include "domain/authenticator_record.h"
#include "steam/confirmation_service.h"
#include "steam/web_api_transport.h"
#include "storage/authenticator_import.h"
#include "storage/storage_error.h"

namespace SteamForge {

[[nodiscard]] QString RecordDisplayName(const AuthenticatorRecord& record);
[[nodiscard]] QString SteamIdText(const AuthenticatorRecord& record);
// The Steam name heads an account once Steam has told it. The name in SteamForge then moves below it, since anyone
// can take the same Steam name, and only then may the user hide it.
[[nodiscard]] bool LeadsWithPersonaName(const AuthenticatorRecord& record, const QString& personaName);
[[nodiscard]] QString AccountTitle(const AuthenticatorRecord& record, const QString& personaName);
// The same for every name, so not even the length of a hidden name shows.
[[nodiscard]] QString HiddenAccountName();
// Where the secrets of the record came from and when, for example "imported from … on …".
[[nodiscard]] QString RecordSourceText(const AuthenticatorRecord& record);
// The same in short, without the file and the date, for example "Imported from Steam Desktop Authenticator".
[[nodiscard]] QString RecordOriginText(const AuthenticatorRecord& record);
[[nodiscard]] QString StorageErrorText(const StorageError& error);
// The technical reason of a failed Steam request, shown after the explanation, for example "Steam error 2".
// Like the Steam mobile application: what failed, why in plain words, what to do, and the code for support last.
[[nodiscard]] QString DescribeWebApiError(const QString& whatFailed, const WebApiError& error, const QString& whatNext = {});
[[nodiscard]] QString WithErrorCode(const QString& text, const WebApiError& error);
[[nodiscard]] QString ImportErrorText(const ImportError& error, const QString& fileName);
// The kind of a mobile confirmation as Steam names it, for example "Trade Offer".
[[nodiscard]] QString ConfirmationTypeText(const PendingConfirmation& confirmation);
// The lines one under another, each after a bullet.
[[nodiscard]] QString BulletLines(const QStringList& lines);

} // namespace SteamForge
