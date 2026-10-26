#include "steam/confirmation_service.h"

#include "steam/web_api_call.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView LIST_PATH{"mobileconf/getlist"};
	constexpr QLatin1StringView SINGLE_RESPONSE_PATH{"mobileconf/ajaxop"};
	constexpr QLatin1StringView BATCH_RESPONSE_PATH{"mobileconf/multiajaxop"};

	// EConfirmationsTag of the Steam mobile application 3.10.9; the signed tag and the operation name differ.
	constexpr QLatin1StringView LIST_TAG{"list"};
	constexpr QLatin1StringView ACCEPT_TAG{"accept"};
	constexpr QLatin1StringView REJECT_TAG{"reject"};
	constexpr QLatin1StringView ACCEPT_OPERATION{"allow"};
	constexpr QLatin1StringView REJECT_OPERATION{"cancel"};
	constexpr QLatin1StringView MOBILE_MODE{"react"};

	constexpr QLatin1StringView DEVICE_ID_PARAMETER{"p"};
	constexpr QLatin1StringView STEAM_ID_PARAMETER{"a"};
	constexpr QLatin1StringView SIGNATURE_PARAMETER{"k"};
	constexpr QLatin1StringView TIME_PARAMETER{"t"};
	constexpr QLatin1StringView MODE_PARAMETER{"m"};
	constexpr QLatin1StringView TAG_PARAMETER{"tag"};
	constexpr QLatin1StringView OPERATION_PARAMETER{"op"};
	constexpr QLatin1StringView CONFIRMATION_ID_PARAMETER{"cid"};
	constexpr QLatin1StringView CONFIRMATION_NONCE_PARAMETER{"ck"};
	constexpr QLatin1StringView BATCH_CONFIRMATION_ID_PARAMETER{"cid[]"};
	constexpr QLatin1StringView BATCH_CONFIRMATION_NONCE_PARAMETER{"ck[]"};

	constexpr QLatin1StringView SUCCESS_KEY{"success"};
	constexpr QLatin1StringView NEEDS_AUTHENTICATION_KEY{"needauth"};
	constexpr QLatin1StringView MESSAGE_KEY{"message"};
	constexpr QLatin1StringView CONFIRMATIONS_KEY{"conf"};
	constexpr QLatin1StringView ID_KEY{"id"};
	constexpr QLatin1StringView NONCE_KEY{"nonce"};
	constexpr QLatin1StringView CREATOR_ID_KEY{"creator_id"};
	constexpr QLatin1StringView TYPE_KEY{"type"};
	constexpr QLatin1StringView TYPE_NAME_KEY{"type_name"};
	constexpr QLatin1StringView HEADLINE_KEY{"headline"};
	constexpr QLatin1StringView SUMMARY_KEY{"summary"};
	constexpr QLatin1StringView WARNINGS_KEY{"warn"};
	constexpr QLatin1StringView ICON_KEY{"icon"};
	constexpr QLatin1StringView BATCH_RESPONSE_KEY{"multi"};
	constexpr QLatin1StringView CREATION_TIME_KEY{"creation_time"};

	// EMobileConfirmationType values of SteamAuth Confirmation.cs.
	constexpr int TRADE_TYPE = 2;
	constexpr int MARKET_LISTING_TYPE = 3;
	constexpr std::size_t TIME_BYTE_COUNT = 8;

	RequestParameters SignedParameters(const ConfirmationKeys& keys, std::chrono::sys_seconds steamTime, QLatin1StringView tag) {
		return {
			{DEVICE_ID_PARAMETER, keys.deviceId},
			{STEAM_ID_PARAMETER, QString::number(keys.steamId)},
			{SIGNATURE_PARAMETER, SignConfirmationRequest(keys.identitySecret, steamTime, tag)},
			{TIME_PARAMETER, QString::number(steamTime.time_since_epoch().count())},
			{MODE_PARAMETER, MOBILE_MODE},
			{TAG_PARAMETER, tag},
		};
	}

	// Steam sends the 64-bit identifiers as text; a number is accepted only while it is exact.
	std::optional<QString> ReadIdentifier(const QJsonValue& value) {
		if (value.isString()) {
			bool isNumber = false;
			QString text = value.toString();
			text.toULongLong(&isNumber);
			if (!isNumber) {
				return std::nullopt;
			}
			return text;
		}
		const qint64 number = value.toInteger(-1);
		if (number < 0) {
			return std::nullopt;
		}
		return QString::number(number);
	}

	QStringList ReadTexts(const QJsonValue& value) {
		QStringList texts;
		for (const auto& item : value.toArray()) {
			if (item.isString()) {
				texts.append(item.toString());
			}
		}
		return texts;
	}

	std::optional<PendingConfirmation> ReadConfirmation(const QJsonObject& object) {
		const std::optional<QString> id = ReadIdentifier(object.value(ID_KEY));
		const std::optional<QString> nonce = ReadIdentifier(object.value(NONCE_KEY));
		if (!id.has_value() || !nonce.has_value()) {
			return std::nullopt;
		}
		PendingConfirmation confirmation{
			.id = *id,
			.nonce = *nonce,
			.creatorId = ReadIdentifier(object.value(CREATOR_ID_KEY)).value_or(QString()),
			.type = object.value(TYPE_KEY).toInt(),
			.category = ConfirmationCategory::OTHER,
			.typeName = object.value(TYPE_NAME_KEY).toString(),
			.headline = object.value(HEADLINE_KEY).toString(),
			.summary = ReadTexts(object.value(SUMMARY_KEY)),
			.warnings = ReadTexts(object.value(WARNINGS_KEY)),
			.iconUrl = QUrl(object.value(ICON_KEY).toString()),
			.allowsBatchResponse = object.value(BATCH_RESPONSE_KEY).toBool(),
			.createdAt = {},
		};
		if (confirmation.type == TRADE_TYPE) {
			confirmation.category = ConfirmationCategory::TRADE;
		} else if (confirmation.type == MARKET_LISTING_TYPE) {
			confirmation.category = ConfirmationCategory::MARKET_LISTING;
		}
		if (const qint64 creationTime = object.value(CREATION_TIME_KEY).toInteger(0); creationTime > 0) {
			confirmation.createdAt = QDateTime::fromSecsSinceEpoch(creationTime, QTimeZone::UTC);
		}
		return confirmation;
	}

	std::expected<QJsonObject, WebApiError> ReadObject(const QByteArray& responseBody) {
		const QJsonDocument document = QJsonDocument::fromJson(responseBody);
		if (!document.isObject()) {
			return std::unexpected(MalformedWebApiResponse());
		}
		return document.object();
	}

	std::expected<void, WebApiError> ReadOutcome(const QJsonObject& object) {
		if (object.value(SUCCESS_KEY).toBool()) {
			return {};
		}
		const WebApiErrorKind kind = object.value(NEEDS_AUTHENTICATION_KEY).toBool() ? WebApiErrorKind::NEEDS_AUTHENTICATION : WebApiErrorKind::UNSUCCESSFUL_RESPONSE;
		return std::unexpected(WebApiError{
			.kind = kind,
			.httpStatus = 0,
			.steamResult = SteamResult::UNKNOWN,
			.message = object.value(MESSAGE_KEY).toString(),
		});
	}
} // namespace

QString SignConfirmationRequest(QByteArrayView identitySecret, std::chrono::sys_seconds steamTime, QLatin1StringView tag) {
	std::array<char, TIME_BYTE_COUNT> timeBytes{};
	qToBigEndian(static_cast<quint64>(steamTime.time_since_epoch().count()), timeBytes.data());
	QByteArray message(timeBytes.data(), static_cast<qsizetype>(timeBytes.size()));
	message.append(QByteArrayView(tag.data(), tag.size()));
	return QString::fromLatin1(QMessageAuthenticationCode::hash(message, identitySecret, QCryptographicHash::Sha1).toBase64());
}

std::expected<std::vector<PendingConfirmation>, WebApiError> ParseConfirmationList(const QByteArray& responseBody) {
	const std::expected<QJsonObject, WebApiError> object = ReadObject(responseBody);
	if (!object.has_value()) {
		return std::unexpected(object.error());
	}
	if (std::expected<void, WebApiError> outcome = ReadOutcome(*object); !outcome.has_value()) {
		return std::unexpected(outcome.error());
	}
	const QJsonValue confirmationsValue = object->value(CONFIRMATIONS_KEY);
	if (!confirmationsValue.isArray() && !confirmationsValue.isUndefined() && !confirmationsValue.isNull()) {
		return std::unexpected(MalformedWebApiResponse());
	}
	std::vector<PendingConfirmation> confirmations;
	for (const auto& item : confirmationsValue.toArray()) {
		// A confirmation that cannot be read fails the whole list instead of silently disappearing from it.
		std::optional<PendingConfirmation> confirmation = ReadConfirmation(item.toObject());
		if (!confirmation.has_value()) {
			return std::unexpected(MalformedWebApiResponse());
		}
		confirmations.push_back(std::move(*confirmation));
	}
	return confirmations;
}

ConfirmationService::ConfirmationService(CommunityTransport& transport) :
	communityTransport(transport) {
}

void ConfirmationService::GetList(const CommunitySession& session, const ConfirmationKeys& keys, std::chrono::sys_seconds steamTime, QObject& context, WebApiHandler<std::vector<PendingConfirmation>> handler) {
	const CommunityRequest request{
		.path = LIST_PATH,
		.httpMethod = WebApiHttpMethod::GET,
		.parameters = SignedParameters(keys, steamTime, LIST_TAG),
		.session = session,
		.extraCookies = {},
	};
	communityTransport.Send(request, context, [resultHandler = std::move(handler)](const WebApiResponse& response) {
		if (!response.has_value()) {
			resultHandler(std::unexpected(response.error()));
			return;
		}
		resultHandler(ParseConfirmationList(*response));
	});
}

void ConfirmationService::Respond(const CommunitySession& session, const ConfirmationKeys& keys, std::chrono::sys_seconds steamTime, std::span<const PendingConfirmation> confirmations, ConfirmationDecision decision, QObject& context, WebApiHandler<void> handler) {
	Q_ASSERT(!confirmations.empty());
	const bool isAccepted = decision == ConfirmationDecision::ACCEPT;
	CommunityRequest request{
		.path = SINGLE_RESPONSE_PATH,
		.httpMethod = WebApiHttpMethod::GET,
		.parameters = {{OPERATION_PARAMETER, isAccepted ? ACCEPT_OPERATION : REJECT_OPERATION}},
		.session = session,
		.extraCookies = {},
	};
	request.parameters.append(SignedParameters(keys, steamTime, isAccepted ? ACCEPT_TAG : REJECT_TAG));
	if (confirmations.size() == 1) {
		request.parameters.append({CONFIRMATION_ID_PARAMETER, confirmations.front().id});
		request.parameters.append({CONFIRMATION_NONCE_PARAMETER, confirmations.front().nonce});
	} else {
		request.path = BATCH_RESPONSE_PATH;
		request.httpMethod = WebApiHttpMethod::POST;
		for (const PendingConfirmation& confirmation : confirmations) {
			request.parameters.append({BATCH_CONFIRMATION_ID_PARAMETER, confirmation.id});
			request.parameters.append({BATCH_CONFIRMATION_NONCE_PARAMETER, confirmation.nonce});
		}
	}
	communityTransport.Send(request, context, [resultHandler = std::move(handler)](const WebApiResponse& response) {
		if (!response.has_value()) {
			resultHandler(std::unexpected(response.error()));
			return;
		}
		const std::expected<QJsonObject, WebApiError> object = ReadObject(*response);
		if (!object.has_value()) {
			resultHandler(std::unexpected(object.error()));
			return;
		}
		resultHandler(ReadOutcome(*object));
	});
}

} // namespace SteamForge
