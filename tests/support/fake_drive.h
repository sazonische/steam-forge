#pragma once

#include "cloud/google_transport.h"

#include <map>

// Google sign-in and an application folder of Drive kept in memory, so several computers in one test share a cloud.
class FakeDrive final : public SteamForge::GoogleTransport {
public:
	void Send(const SteamForge::GoogleRequest& request, QObject& context, SteamForge::GoogleResponseHandler handler) override {
		PendingResponse pending{.context = &context, .response = Answer(request), .handler = std::move(handler), .beforeAnswer = {}};
		if (request.method == SteamForge::GoogleHttpMethod::GET && request.url.path().startsWith(FILES_PATH_PREFIX)) {
			pending.beforeAnswer = std::exchange(nextDownloadAction, {});
		}
		if (isHoldingResponses) {
			heldResponses.push_back(std::move(pending));
			return;
		}
		Deliver(std::move(pending));
	}

	// Runs the action just before the next download is answered, as a change made while the file is on its way.
	void BeforeNextDownload(std::function<void()> action) {
		nextDownloadAction = std::move(action);
	}

	// Answers wait until released, as on a slow network; an answer whose requester is gone by then never comes.
	void HoldResponses() {
		isHoldingResponses = true;
	}

	void ReleaseResponses() {
		isHoldingResponses = false;
		for (PendingResponse& pending : std::exchange(heldResponses, {})) {
			Deliver(std::move(pending));
		}
	}

	[[nodiscard]] qsizetype FileCount() const {
		return static_cast<qsizetype>(files.size());
	}

	// Whether Google could read the text in the name, the properties or the content of a file.
	[[nodiscard]] bool Shows(QByteArrayView text) const {
		return std::ranges::any_of(files, [text](const auto& entry) {
			const StoredFile& file = entry.second;
			return file.name.toUtf8().contains(text) || QJsonDocument(file.properties).toJson().contains(text) || file.content.contains(text);
		});
	}

	// The files of one kind, such as "record".
	[[nodiscard]] qsizetype FileCount(const QString& kind) const {
		return std::ranges::count_if(files, [&kind](const auto& entry) {
			return entry.second.properties.value(QStringLiteral("kind")).toString() == kind;
		});
	}

	// Changes one byte in the middle of every file of the kind, as a damaged upload would.
	void DamageFiles(const QString& kind) {
		for (auto& entry : files) {
			StoredFile& file = entry.second;
			if (file.properties.value(QStringLiteral("kind")).toString() == kind && !file.content.isEmpty()) {
				const qsizetype middle = file.content.size() / 2;
				file.content[middle] = static_cast<char>(file.content.at(middle) ^ 1);
			}
		}
	}

private:
	static constexpr QLatin1StringView FILES_PATH_PREFIX{"/drive/v3/files/"};

	struct StoredFile {
		QString name;
		QJsonObject properties;
		QByteArray content;
		QDateTime createdTime;
	};

	struct PendingResponse {
		QPointer<QObject> context;
		SteamForge::GoogleResponse response;
		SteamForge::GoogleResponseHandler handler;
		std::function<void()> beforeAnswer;
	};

	static void Deliver(PendingResponse pending) {
		if (pending.context == nullptr) {
			return;
		}
		QObject* context = pending.context;
		QTimer::singleShot(0, context, [delivered = std::move(pending)]() {
			if (delivered.beforeAnswer) {
				delivered.beforeAnswer();
			}
			delivered.handler(delivered.response);
		});
	}

	SteamForge::GoogleResponse Answer(const SteamForge::GoogleRequest& request) {
		const QString path = request.url.path();
		if (path.endsWith(QStringLiteral("/token"))) {
			return TokenAnswer();
		}
		if (request.method == SteamForge::GoogleHttpMethod::POST && path.contains(QStringLiteral("/upload/"))) {
			return Upload(request);
		}
		if (path.startsWith(FILES_PATH_PREFIX)) {
			const QString fileId = path.mid(FILES_PATH_PREFIX.size());
			const auto file = files.find(fileId);
			if (file == files.end()) {
				return std::unexpected(SteamForge::GoogleError{.kind = SteamForge::GoogleErrorKind::HTTP_STATUS, .httpStatus = 404, .reason = QStringLiteral("NOT_FOUND")});
			}
			if (request.method == SteamForge::GoogleHttpMethod::DELETE_RESOURCE) {
				files.erase(file);
				return QByteArray();
			}
			return file->second.content;
		}
		return List();
	}

	static SteamForge::GoogleResponse TokenAnswer() {
		const QByteArray payload = QJsonDocument(QJsonObject{{QStringLiteral("email"), QStringLiteral("player@example.com")}}).toJson(QJsonDocument::Compact);
		const QString idToken = QStringLiteral("e30.") + QString::fromLatin1(payload.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)) + QStringLiteral(".signature");
		return QJsonDocument(QJsonObject{
								 {QStringLiteral("access_token"), QStringLiteral("access")},
								 {QStringLiteral("refresh_token"), QStringLiteral("synthetic-refresh-token")},
								 {QStringLiteral("expires_in"), 3599},
								 {QStringLiteral("scope"), QStringLiteral("openid https://www.googleapis.com/auth/drive.appdata")},
								 {QStringLiteral("id_token"), idToken},
							 })
			.toJson(QJsonDocument::Compact);
	}

	SteamForge::GoogleResponse Upload(const SteamForge::GoogleRequest& request) {
		constexpr QByteArrayView BOUNDARY_PREFIX{"boundary="};
		constexpr QByteArrayView PART_HEADER_END{"\r\n\r\n"};
		const QByteArray separator = "\r\n--" + request.contentType.mid(request.contentType.indexOf(BOUNDARY_PREFIX) + BOUNDARY_PREFIX.size());
		const qsizetype metadataStart = request.body.indexOf(PART_HEADER_END) + PART_HEADER_END.size();
		const qsizetype metadataEnd = request.body.indexOf(separator, metadataStart);
		const qsizetype contentStart = request.body.indexOf(PART_HEADER_END, metadataEnd) + PART_HEADER_END.size();
		const qsizetype contentEnd = request.body.indexOf(separator, contentStart);
		const QJsonObject metadata = QJsonDocument::fromJson(request.body.sliced(metadataStart, metadataEnd - metadataStart)).object();
		const QString fileId = QStringLiteral("file-%1").arg(++fileCount);
		// Strictly increasing times keep "oldest first" meaningful.
		const QDateTime createdTime = QDateTime(QDate(2026, 10, 27), QTime(12, 0), QTimeZone::UTC).addSecs(fileCount);
		files[fileId] = StoredFile{.name = metadata.value(QStringLiteral("name")).toString(), .properties = metadata.value(QStringLiteral("appProperties")).toObject(), .content = request.body.sliced(contentStart, contentEnd - contentStart), .createdTime = createdTime};
		return QJsonDocument(FileObject(fileId, files[fileId])).toJson(QJsonDocument::Compact);
	}

	SteamForge::GoogleResponse List() const {
		std::vector<std::pair<QString, const StoredFile*>> ordered;
		for (const auto& [fileId, file] : files) {
			ordered.emplace_back(fileId, &file);
		}
		std::ranges::sort(ordered, {}, [](const auto& entry) {
			return entry.second->createdTime;
		});
		QJsonArray listed;
		for (const auto& [fileId, file] : ordered) {
			listed.append(FileObject(fileId, *file));
		}
		return QJsonDocument(QJsonObject{{QStringLiteral("files"), listed}}).toJson(QJsonDocument::Compact);
	}

	static QJsonObject FileObject(const QString& fileId, const StoredFile& file) {
		return QJsonObject{
			{QStringLiteral("id"), fileId},
			{QStringLiteral("name"), file.name},
			{QStringLiteral("appProperties"), file.properties},
			{QStringLiteral("createdTime"), file.createdTime.toString(Qt::ISODateWithMs)},
		};
	}

	std::map<QString, StoredFile> files;
	int fileCount = 0;
	std::function<void()> nextDownloadAction;
	bool isHoldingResponses = false;
	std::vector<PendingResponse> heldResponses;
};
