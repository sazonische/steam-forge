#pragma once

#include "app/account_library.h"
#include "app/cloud_sync.h"
#include "app/google_connection.h"
#include "app/storage_encryption.h"

#include "support/fake_drive.h"
#include "support/fake_key_store.h"
#include "support/loopback_browser.h"

inline SteamForge::GoogleEndpoints FakeDriveEndpoints() {
	return SteamForge::GoogleEndpoints{
		.authorizationUrl = QUrl(QStringLiteral("https://accounts.test/auth")),
		.tokenUrl = QUrl(QStringLiteral("https://oauth.test/token")),
		.revocationUrl = QUrl(QStringLiteral("https://oauth.test/revoke")),
		.driveFilesUrl = QUrl(QStringLiteral("https://drive.test/drive/v3/files")),
		.driveUploadUrl = QUrl(QStringLiteral("https://drive.test/upload/drive/v3/files")),
	};
}

// One computer: its own data folder, encryption, Google connection and sync, sharing the cloud of the test.
struct CloudComputer {
	QTemporaryDir directory;
	FakeKeyStore keyStore;
	std::unique_ptr<SteamForge::AccountLibrary> library;
	std::unique_ptr<SteamForge::StorageEncryption> encryption;
	std::unique_ptr<SteamForge::GoogleConnection> connection;
	std::unique_ptr<SteamForge::CloudSync> sync;

	explicit CloudComputer(FakeDrive& drive) {
		library = std::make_unique<SteamForge::AccountLibrary>(SteamForge::AuthenticatorStore(directory.filePath(QStringLiteral("accounts"))), SteamForge::SessionStore(directory.filePath(QStringLiteral("sessions"))));
		encryption = std::make_unique<SteamForge::StorageEncryption>(keyStore, *library, directory.filePath(QStringLiteral("encryption.json")));
		encryption->PrepareLibrary();
		library->Load();
		if (!encryption->Enable(SteamForge::KeyProtection::USER_ACCOUNT).has_value()) {
			qFatal("Encryption could not be turned on for the test.");
		}
		const SteamForge::GoogleClient client{.clientId = QStringLiteral("client-id"), .clientSecret = {}};
		connection = std::make_unique<SteamForge::GoogleConnection>(drive, *encryption, client, FakeDriveEndpoints(), directory.filePath(QStringLiteral("google.json")));
		sync = std::make_unique<SteamForge::CloudSync>(*connection, *library);
	}

	[[nodiscard]] qsizetype BackupCount() const {
		return QDir(directory.filePath(QStringLiteral("backups"))).entryList(QDir::Files).size();
	}

	[[nodiscard]] const SteamForge::AuthenticatorRecord* RecordOf(quint64 steamId) const {
		const auto found = std::ranges::find_if(library->Records(), [steamId](const SteamForge::AuthenticatorRecord& record) {
			return record.account.steamId == steamId;
		});
		return found == library->Records().end() ? nullptr : &*found;
	}
};

// Signs in like a browser; a new vault is saved, an existing one opens with the key.
inline QString ConnectToGoogle(CloudComputer& computer, const QString& recoveryKeyText = {}) {
	QSignalSpy pageSpy(computer.connection.get(), &SteamForge::GoogleConnection::SignInPageReady);
	QSignalSpy newVaultSpy(computer.connection.get(), &SteamForge::GoogleConnection::NewVaultCreated);
	QSignalSpy keyNeededSpy(computer.connection.get(), &SteamForge::GoogleConnection::RecoveryKeyNeeded);
	QSignalSpy connectedSpy(computer.connection.get(), &SteamForge::GoogleConnection::Connected);
	computer.connection->Connect();
	if (pageSpy.count() != 1) {
		return {};
	}
	const QUrlQuery query(pageSpy.front().front().toUrl());
	const QUrl redirectUri(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded));
	const QByteArray state = QUrl::toPercentEncoding(query.queryItemValue(QStringLiteral("state"), QUrl::FullyDecoded));
	VisitLoopback(static_cast<quint16>(redirectUri.port()), "/?state=" + state + "&code=synthetic-code");
	if (!QTest::qWaitFor([&newVaultSpy, &keyNeededSpy]() {
			return newVaultSpy.count() > 0 || keyNeededSpy.count() > 0;
		})) {
		return {};
	}
	QString keyText = recoveryKeyText;
	if (newVaultSpy.count() > 0) {
		keyText = newVaultSpy.front().front().toString();
		computer.connection->ConfirmRecoveryKeySaved();
	} else if (const std::optional<SteamForge::RecoveryKey> recoveryKey = SteamForge::RecoveryKey::Parse(recoveryKeyText); recoveryKey.has_value()) {
		computer.connection->OpenVault(*recoveryKey);
	}
	if (!QTest::qWaitFor([&connectedSpy]() {
			return connectedSpy.count() > 0;
		})) {
		return {};
	}
	return keyText;
}

inline bool SyncAndWait(SteamForge::CloudSync& sync) {
	sync.Synchronize();
	return QTest::qWaitFor([&sync]() {
		return sync.Status() != SteamForge::CloudSyncStatus::SYNCING;
	});
}

inline bool AddAndWait(CloudComputer& computer, const QUuid& recordId) {
	computer.sync->AddToCloud(recordId);
	return QTest::qWaitFor([&computer, &recordId]() {
		return computer.sync->IsInCloud(recordId);
	});
}
