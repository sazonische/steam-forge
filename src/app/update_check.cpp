#include "app/update_check.h"

#include "common/constants.h"
#include "steam/steam_http.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView RELEASE_PAGE_URL_PATTERN{"https://github.com/sazonische/steam-forge/releases/tag/v%1"};
	// The parts are limited like the release workflow limits them for the Windows version resource.
	constexpr QLatin1StringView RELEASE_TAG_PATTERN{R"(^v(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})\.(0|[1-9][0-9]{0,4})$)"};
	constexpr QLatin1StringView TAG_NAME_FIELD{"tag_name"};
	constexpr QLatin1StringView DRAFT_FIELD{"draft"};
	constexpr QLatin1StringView PRERELEASE_FIELD{"prerelease"};
	constexpr QByteArrayView GITHUB_JSON_MEDIA_TYPE{"application/vnd.github+json"};
	constexpr QLatin1StringView USER_AGENT_PATTERN{"%1/%2"};
	constexpr int MAXIMUM_REDIRECTS = 2;
	// Leaves the start to the Steam requests the window needs first.
	constexpr std::chrono::milliseconds FIRST_CHECK_DELAY{std::chrono::seconds(5)};
	// The program often runs in the tray for weeks.
	constexpr std::chrono::milliseconds CHECK_INTERVAL{std::chrono::hours(24)};
	constexpr std::chrono::milliseconds REQUEST_TIMEOUT{std::chrono::seconds(20)};
} // namespace

std::optional<QVersionNumber> ParseLatestReleaseVersion(const QByteArray& releaseJson) {
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(releaseJson, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return std::nullopt;
	}
	const QJsonObject release = document.object();
	if (release.value(DRAFT_FIELD).toBool() || release.value(PRERELEASE_FIELD).toBool()) {
		return std::nullopt;
	}
	const QRegularExpressionMatch tagMatch = QRegularExpression(RELEASE_TAG_PATTERN).match(release.value(TAG_NAME_FIELD).toString());
	if (!tagMatch.hasMatch()) {
		return std::nullopt;
	}
	return QVersionNumber(tagMatch.captured(1).toInt(), tagMatch.captured(2).toInt(), tagMatch.captured(3).toInt());
}

QUrl ReleasePageUrl(const QVersionNumber& version) {
	return QUrl(QString(RELEASE_PAGE_URL_PATTERN).arg(version.toString()));
}

UpdateCheck::UpdateCheck(QVersionNumber runningVersion, QUrl latestReleaseUrl, QObject* parent) :
	QObject(parent),
	currentVersion(std::move(runningVersion)),
	releaseUrl(std::move(latestReleaseUrl)) {
	connect(&checkTimer, &QTimer::timeout, this, &UpdateCheck::RunScheduledCheck);
}

void UpdateCheck::Start() {
	checkTimer.start(FIRST_CHECK_DELAY);
}

void UpdateCheck::Stop() {
	checkTimer.stop();
	if (QNetworkReply* reply = pendingReply.data(); reply != nullptr) {
		reply->abort();
	}
}

void UpdateCheck::CheckNow() {
	if (pendingReply != nullptr) {
		return;
	}
	QNetworkRequest networkRequest(releaseUrl);
	networkRequest.setTransferTimeout(REQUEST_TIMEOUT);
	networkRequest.setRawHeader("Accept", GITHUB_JSON_MEDIA_TYPE.toByteArray());
	// GitHub refuses API requests without a User-Agent.
	networkRequest.setHeader(QNetworkRequest::UserAgentHeader, QString(USER_AGENT_PATTERN).arg(APPLICATION_NAME, currentVersion.toString()));
	networkRequest.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
	networkRequest.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
	networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
	networkRequest.setMaximumRedirectsAllowed(MAXIMUM_REDIRECTS);
	pendingReply = networkAccessManager.get(networkRequest);
	WatchReply(pendingReply, *this, [this](QNetworkReply& finishedReply) {
		HandleReply(finishedReply);
	});
}

bool UpdateCheck::IsChecking() const {
	return pendingReply != nullptr;
}

void UpdateCheck::RunScheduledCheck() {
	checkTimer.start(CHECK_INTERVAL);
	CheckNow();
}

void UpdateCheck::HandleReply(QNetworkReply& reply) {
	pendingReply = nullptr;
	// Offline, a rate limit or no release yet: the next check asks again.
	if (ReplyFailure(reply).has_value()) {
		return;
	}
	const std::optional<QVersionNumber> latestVersion = ParseLatestReleaseVersion(reply.readAll());
	if (latestVersion.has_value() && QVersionNumber::compare(*latestVersion, currentVersion) > 0) {
		Q_EMIT NewerReleaseFound(*latestVersion);
	}
}

} // namespace SteamForge
