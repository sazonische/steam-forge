#pragma once

namespace SteamForge {

// GitHub answers with the newest published release; drafts and prereleases are never returned.
inline constexpr QLatin1StringView LATEST_RELEASE_API_URL{"https://api.github.com/repos/sazonische/steam-forge/releases/latest"};

// The version of a release answer of GitHub, when the answer is a published release tagged vMAJOR.MINOR.PATCH.
[[nodiscard]] std::optional<QVersionNumber> ParseLatestReleaseVersion(const QByteArray& releaseJson);

// The release page with the files of the version.
[[nodiscard]] QUrl ReleasePageUrl(const QVersionNumber& version);

// Asks GitHub soon after the start and then once a day whether a newer release is out.
// Nothing is downloaded or installed; a failed check stays silent and the next one tries again.
class UpdateCheck final : public QObject {
	Q_OBJECT

public:
	UpdateCheck(QVersionNumber runningVersion, QUrl latestReleaseUrl, QObject* parent = nullptr);

	void Start();
	void Stop();
	void CheckNow();
	[[nodiscard]] bool IsChecking() const;

Q_SIGNALS:
	void NewerReleaseFound(const QVersionNumber& version);

private:
	void RunScheduledCheck();
	void HandleReply(QNetworkReply& reply);

	QVersionNumber currentVersion;
	QUrl releaseUrl;
	QNetworkAccessManager networkAccessManager;
	QTimer checkTimer;
	QPointer<QNetworkReply> pendingReply;
};

} // namespace SteamForge
