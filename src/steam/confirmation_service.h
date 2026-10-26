#pragma once

#include "steam/community_transport.h"

namespace SteamForge {

enum class ConfirmationCategory {
	TRADE,
	MARKET_LISTING,
	OTHER
};

// One entry of the mobile confirmations list; Steam supplies all texts.
struct PendingConfirmation {
	// 64-bit numbers kept as the decimal text Steam sent.
	QString id;
	QString nonce;
	// The trade offer or market listing the confirmation is for.
	QString creatorId;
	int type = 0;
	ConfirmationCategory category = ConfirmationCategory::OTHER;
	QString typeName;
	QString headline;
	QStringList summary;
	QStringList warnings;
	// The avatar of the trade partner or the picture of the listed item.
	QUrl iconUrl;
	// Steam accepts a decision on it together with others; the rest are decided one at a time.
	bool allowsBatchResponse = false;
	QDateTime createdAt;
};

enum class ConfirmationDecision {
	ACCEPT,
	REJECT
};

// What signs the confirmation requests of one account.
struct ConfirmationKeys {
	quint64 steamId = 0;
	QString deviceId;
	QByteArray identitySecret;

	bool operator==(const ConfirmationKeys&) const = default;
};

// Base64 of HMAC-SHA1 with the identity secret over the time (uint64, big endian) followed by the tag.
[[nodiscard]] QString SignConfirmationRequest(QByteArrayView identitySecret, std::chrono::sys_seconds steamTime, QLatin1StringView tag);

[[nodiscard]] std::expected<std::vector<PendingConfirmation>, WebApiError> ParseConfirmationList(const QByteArray& responseBody);

// The mobile confirmations of steamcommunity.com/mobileconf as the Steam mobile application 3.10.9 uses them.
class ConfirmationService final {
public:
	explicit ConfirmationService(CommunityTransport& transport);

	void GetList(const CommunitySession& session, const ConfirmationKeys& keys, std::chrono::sys_seconds steamTime, QObject& context, WebApiHandler<std::vector<PendingConfirmation>> handler);
	// One confirmation is sent alone; several only when all of them allow a batch response.
	void Respond(const CommunitySession& session, const ConfirmationKeys& keys, std::chrono::sys_seconds steamTime, std::span<const PendingConfirmation> confirmations, ConfirmationDecision decision, QObject& context, WebApiHandler<void> handler);

private:
	CommunityTransport& communityTransport;
};

} // namespace SteamForge
