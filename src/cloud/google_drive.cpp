#include "cloud/google_drive.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView APPLICATION_DATA_FOLDER{"appDataFolder"};
	constexpr QLatin1StringView FILE_FIELDS{"id,name,appProperties,createdTime"};
	constexpr QLatin1StringView LIST_FIELDS{"nextPageToken,files(id,name,appProperties,createdTime)"};
	constexpr QLatin1StringView MAXIMUM_PAGE_SIZE{"1000"};
	constexpr QLatin1StringView ID_FIELD{"id"};
	constexpr QLatin1StringView NAME_FIELD{"name"};
	constexpr QLatin1StringView PARENTS_FIELD{"parents"};
	constexpr QLatin1StringView APP_PROPERTIES_FIELD{"appProperties"};
	constexpr QLatin1StringView CREATED_TIME_FIELD{"createdTime"};
	constexpr QLatin1StringView FILES_FIELD{"files"};
	constexpr QLatin1StringView NEXT_PAGE_TOKEN_FIELD{"nextPageToken"};
	constexpr QLatin1StringView MULTIPART_CONTENT_TYPE_PATTERN{"multipart/related; boundary=%1"};
	constexpr QLatin1StringView METADATA_PART_PATTERN{"--%1\r\nContent-Type: application/json; charset=UTF-8\r\n\r\n"};
	constexpr QLatin1StringView CONTENT_PART_PATTERN{"\r\n--%1\r\nContent-Type: application/octet-stream\r\n\r\n"};
	constexpr QLatin1StringView CLOSING_PATTERN{"\r\n--%1--"};

	GoogleError MalformedResponse() {
		return GoogleError{.kind = GoogleErrorKind::MALFORMED_RESPONSE, .httpStatus = 0, .reason = {}};
	}

	std::optional<DriveFile> ParseFile(const QJsonObject& object) {
		const QString fileId = object.value(ID_FIELD).toString();
		if (fileId.isEmpty()) {
			return std::nullopt;
		}
		DriveProperties appProperties;
		const QJsonObject properties = object.value(APP_PROPERTIES_FIELD).toObject();
		for (auto property = properties.begin(); property != properties.end(); ++property) {
			appProperties.insert(property.key(), property.value().toString());
		}
		return DriveFile{
			.id = fileId,
			.name = object.value(NAME_FIELD).toString(),
			.appProperties = std::move(appProperties),
			.createdTime = QDateTime::fromString(object.value(CREATED_TIME_FIELD).toString(), Qt::ISODateWithMs),
		};
	}

	// A boundary that does not occur in the uploaded bytes.
	QString MultipartBoundary(const QByteArray& content) {
		QString boundary;
		do {
			boundary = QUuid::createUuid().toString(QUuid::Id128);
		} while (content.contains(boundary.toLatin1()));
		return boundary;
	}
} // namespace

GoogleDrive::GoogleDrive(GoogleTransport& transport, GoogleAccess& access, GoogleEndpoints endpoints, QObject* parent) :
	QObject(parent),
	googleTransport(transport),
	googleAccess(access),
	googleEndpoints(std::move(endpoints)) {
}

void GoogleDrive::ListFiles(QObject& context, GoogleHandler<std::vector<DriveFile>> handler) {
	ListPage({}, {}, context, std::move(handler));
}

void GoogleDrive::ListPage(const QString& pageToken, std::vector<DriveFile> collectedFiles, QObject& context, GoogleHandler<std::vector<DriveFile>> handler) {
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("spaces"), APPLICATION_DATA_FOLDER);
	query.addQueryItem(QStringLiteral("pageSize"), MAXIMUM_PAGE_SIZE);
	query.addQueryItem(QStringLiteral("orderBy"), QStringLiteral("createdTime"));
	query.addQueryItem(QStringLiteral("fields"), LIST_FIELDS);
	if (!pageToken.isEmpty()) {
		query.addQueryItem(QStringLiteral("pageToken"), pageToken);
	}
	QUrl url = googleEndpoints.driveFilesUrl;
	url.setQuery(query);
	const QPointer<QObject> guardedContext(&context);
	SendAuthorized(GoogleRequest{.method = GoogleHttpMethod::GET, .url = url, .contentType = {}, .body = {}, .accessToken = {}}, context, [this, guardedContext, filesSoFar = std::move(collectedFiles), listHandler = std::move(handler)](GoogleResponse response) mutable {
		if (!response.has_value()) {
			listHandler(std::unexpected(response.error()));
			return;
		}
		const QJsonDocument document = QJsonDocument::fromJson(*response);
		if (!document.isObject()) {
			listHandler(std::unexpected(MalformedResponse()));
			return;
		}
		for (const auto& fileValue : document.object().value(FILES_FIELD).toArray()) {
			std::optional<DriveFile> file = ParseFile(fileValue.toObject());
			if (!file.has_value()) {
				listHandler(std::unexpected(MalformedResponse()));
				return;
			}
			filesSoFar.push_back(std::move(*file));
		}
		const QString nextPageToken = document.object().value(NEXT_PAGE_TOKEN_FIELD).toString();
		if (nextPageToken.isEmpty() || guardedContext == nullptr) {
			listHandler(std::move(filesSoFar));
			return;
		}
		ListPage(nextPageToken, std::move(filesSoFar), *guardedContext, std::move(listHandler));
	});
}

void GoogleDrive::Download(const QString& fileId, QObject& context, GoogleHandler<QByteArray> handler) {
	QUrl url = FileUrl(fileId);
	url.setQuery(QStringLiteral("alt=media"));
	SendAuthorized(GoogleRequest{.method = GoogleHttpMethod::GET, .url = url, .contentType = {}, .body = {}, .accessToken = {}}, context, [downloadHandler = std::move(handler)](GoogleResponse response) {
		downloadHandler(std::move(response));
	});
}

void GoogleDrive::Create(const QString& name, const DriveProperties& appProperties, const QByteArray& content, QObject& context, GoogleHandler<DriveFile> handler) {
	QJsonObject properties;
	for (auto property = appProperties.begin(); property != appProperties.end(); ++property) {
		properties.insert(property.key(), property.value());
	}
	const QJsonObject metadata{
		{NAME_FIELD, name},
		{PARENTS_FIELD, QJsonArray{APPLICATION_DATA_FOLDER}},
		{APP_PROPERTIES_FIELD, properties},
	};
	const QString boundary = MultipartBoundary(content);
	QByteArray body = QString(METADATA_PART_PATTERN).arg(boundary).toLatin1();
	body += QJsonDocument(metadata).toJson(QJsonDocument::Compact);
	body += QString(CONTENT_PART_PATTERN).arg(boundary).toLatin1();
	body += content;
	body += QString(CLOSING_PATTERN).arg(boundary).toLatin1();

	QUrl url = googleEndpoints.driveUploadUrl;
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("uploadType"), QStringLiteral("multipart"));
	query.addQueryItem(QStringLiteral("fields"), FILE_FIELDS);
	url.setQuery(query);
	const GoogleRequest request{
		.method = GoogleHttpMethod::POST,
		.url = url,
		.contentType = QString(MULTIPART_CONTENT_TYPE_PATTERN).arg(boundary).toLatin1(),
		.body = std::move(body),
		.accessToken = {},
	};
	SendAuthorized(request, context, [createHandler = std::move(handler)](GoogleResponse response) {
		if (!response.has_value()) {
			createHandler(std::unexpected(response.error()));
			return;
		}
		std::optional<DriveFile> file = ParseFile(QJsonDocument::fromJson(*response).object());
		if (!file.has_value()) {
			createHandler(std::unexpected(MalformedResponse()));
			return;
		}
		createHandler(std::move(*file));
	});
}

void GoogleDrive::Delete(const QString& fileId, QObject& context, GoogleHandler<void> handler) {
	SendAuthorized(GoogleRequest{.method = GoogleHttpMethod::DELETE_RESOURCE, .url = FileUrl(fileId), .contentType = {}, .body = {}, .accessToken = {}}, context, [deleteHandler = std::move(handler)](GoogleResponse response) {
		if (!response.has_value()) {
			deleteHandler(std::unexpected(response.error()));
			return;
		}
		deleteHandler({});
	});
}

void GoogleDrive::SendAuthorized(GoogleRequest request, QObject& context, GoogleResponseHandler handler, bool isRepeated) {
	const QPointer<QObject> guardedContext(&context);
	googleAccess.RequestAccessToken(*this, [this, pendingRequest = std::move(request), guardedContext, responseHandler = std::move(handler), isRepeated](std::expected<QString, GoogleError> accessToken) mutable {
		if (guardedContext == nullptr) {
			return;
		}
		if (!accessToken.has_value()) {
			responseHandler(std::unexpected(accessToken.error()));
			return;
		}
		pendingRequest.accessToken = *accessToken;
		googleTransport.Send(pendingRequest, *this, [this, sentRequest = pendingRequest, guardedContext, sentHandler = std::move(responseHandler), isRepeated](GoogleResponse response) mutable {
			if (guardedContext == nullptr) {
				return;
			}
			if (!response.has_value() && response.error().kind == GoogleErrorKind::UNAUTHORIZED && !isRepeated) {
				googleAccess.ForgetAccessToken();
				SendAuthorized(std::move(sentRequest), *guardedContext, std::move(sentHandler), true);
				return;
			}
			sentHandler(std::move(response));
		});
	});
}

QUrl GoogleDrive::FileUrl(const QString& fileId) const {
	QUrl url = googleEndpoints.driveFilesUrl;
	url.setPath(url.path() + u'/' + QString::fromLatin1(QUrl::toPercentEncoding(fileId)));
	return url;
}

} // namespace SteamForge
