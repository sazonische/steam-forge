#include "app/google_connection.h"

#include "app/storage_encryption.h"
#include "cloud/google_redirect_receiver.h"
#include "common/encoding.h"
#include "storage/private_file.h"
#include "storage/sealed_file.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView STATE_FORMAT{"steamforge-google"};
	constexpr int STATE_FORMAT_VERSION = 1;
	constexpr QLatin1StringView FORMAT_FIELD{"format"};
	constexpr QLatin1StringView VERSION_FIELD{"version"};
	constexpr QLatin1StringView EMAIL_FIELD{"email"};
	constexpr QLatin1StringView REFRESH_TOKEN_FIELD{"refreshToken"};
	constexpr QLatin1StringView VAULT_ID_FIELD{"vaultId"};
	constexpr QLatin1StringView VAULT_FILE_ID_FIELD{"vaultFileId"};
	constexpr QLatin1StringView CLOUD_KEY_FIELD{"cloudKey"};
	constexpr QLatin1StringView LINKS_FIELD{"links"};
	constexpr QLatin1StringView LOCAL_RECORD_ID_FIELD{"localRecordId"};
	constexpr QLatin1StringView CLOUD_RECORD_ID_FIELD{"cloudRecordId"};
	constexpr QLatin1StringView REVISION_FIELD{"revision"};
	constexpr QLatin1StringView FILE_ID_FIELD{"fileId"};
	constexpr QLatin1StringView FINGERPRINT_FIELD{"fingerprint"};
	constexpr QLatin1StringView CLOUD_ONLY_FIELD{"cloudOnly"};
	constexpr QLatin1StringView TITLE_FIELD{"title"};
	// The links grow by a few hundred bytes per account in Google.
	constexpr qint64 MAX_STATE_DOCUMENT_SIZE = qint64{1} << 20;
	constexpr QLatin1StringView VAULT_FILE_NAME{"vault.json"};
	constexpr QLatin1StringView KIND_PROPERTY{"kind"};
	constexpr QLatin1StringView VAULT_KIND{"vault"};
	constexpr QLatin1StringView VAULT_ID_PROPERTY{"vaultId"};
	constexpr QLatin1StringView ACCESS_DENIED_REASON{"access_denied"};

	bool IsVaultFile(const DriveFile& file) {
		return file.name == VAULT_FILE_NAME && file.appProperties.value(KIND_PROPERTY) == VAULT_KIND;
	}

	// The list comes oldest first: when two computers created a vault at once, the older one wins.
	std::optional<DriveFile> OldestVaultFile(const std::vector<DriveFile>& files) {
		const auto vaultFile = std::ranges::find_if(files, IsVaultFile);
		if (vaultFile == files.end()) {
			return std::nullopt;
		}
		return *vaultFile;
	}

	GoogleConnectFailure HeaderFailure(VaultHeaderError error) {
		return error == VaultHeaderError::NEWER_VERSION ? GoogleConnectFailure::VAULT_NEWER_VERSION : GoogleConnectFailure::VAULT_DAMAGED;
	}

	std::vector<CloudLink> ReadLinks(const QJsonArray& savedLinks) {
		std::vector<CloudLink> readLinks;
		for (const auto& linkValue : savedLinks) {
			const QJsonObject link = linkValue.toObject();
			const QUuid localRecordId = QUuid::fromString(link.value(LOCAL_RECORD_ID_FIELD).toString());
			const QString fileId = link.value(FILE_ID_FIELD).toString();
			if (!localRecordId.isNull() && !fileId.isEmpty()) {
				readLinks.push_back(CloudLink{.localRecordId = localRecordId, .cloudRecordId = link.value(CLOUD_RECORD_ID_FIELD).toString(), .revision = link.value(REVISION_FIELD).toInt(1), .fileId = fileId, .fingerprint = link.value(FINGERPRINT_FIELD).toString()});
			}
		}
		return readLinks;
	}

	QJsonArray WriteLinks(const std::vector<CloudLink>& savedLinks) {
		QJsonArray written;
		for (const CloudLink& link : savedLinks) {
			written.append(QJsonObject{
				{LOCAL_RECORD_ID_FIELD, link.localRecordId.toString(QUuid::WithoutBraces)},
				{CLOUD_RECORD_ID_FIELD, link.cloudRecordId},
				{REVISION_FIELD, link.revision},
				{FILE_ID_FIELD, link.fileId},
				{FINGERPRINT_FIELD, link.fingerprint},
			});
		}
		return written;
	}

	std::vector<CloudOnlyRecord> ReadCloudOnlyRecords(const QJsonArray& savedRecords) {
		std::vector<CloudOnlyRecord> readRecords;
		for (const auto& recordValue : savedRecords) {
			const QJsonObject record = recordValue.toObject();
			const QString cloudRecordId = record.value(CLOUD_RECORD_ID_FIELD).toString();
			if (!cloudRecordId.isEmpty()) {
				readRecords.push_back(CloudOnlyRecord{.cloudRecordId = cloudRecordId, .title = record.value(TITLE_FIELD).toString()});
			}
		}
		return readRecords;
	}

	QJsonArray WriteCloudOnlyRecords(const std::vector<CloudOnlyRecord>& savedRecords) {
		QJsonArray written;
		for (const CloudOnlyRecord& record : savedRecords) {
			written.append(QJsonObject{
				{CLOUD_RECORD_ID_FIELD, record.cloudRecordId},
				{TITLE_FIELD, record.title},
			});
		}
		return written;
	}

	template<typename Owned>
	void DeleteLater(std::unique_ptr<Owned>& object) {
		// Callbacks of these objects may be running further up the stack.
		if (object != nullptr) {
			object.release()->deleteLater();
		}
	}
} // namespace

struct GoogleConnection::SignInAttempt {
	quint64 number = 0;
	GoogleConnectionState stateBefore = GoogleConnectionState::DISCONNECTED;
	ProofKey proofKey;
	QString redirectUri;
	std::unique_ptr<GoogleRedirectReceiver> receiver;
	QString email;
	std::unique_ptr<GoogleAccess> access;
	std::unique_ptr<GoogleDrive> drive;
	std::optional<CloudVaultHeader> header;
	QString vaultFileId;
	std::optional<DataKey> cloudKey;
	std::optional<RecoveryKey> recoveryKey;
	bool isReplacingVault = false;
};

GoogleConnection::GoogleConnection(GoogleTransport& transport, StorageEncryption& encryption, GoogleClient client, GoogleEndpoints endpoints, QString stateFilePath, QObject* parent) :
	QObject(parent),
	googleTransport(transport),
	storageEncryption(encryption),
	googleClient(std::move(client)),
	googleEndpoints(std::move(endpoints)),
	filePath(std::move(stateFilePath)) {
	if (googleClient.clientId.isEmpty()) {
		state = GoogleConnectionState::UNAVAILABLE;
	}
}

GoogleConnection::~GoogleConnection() {
	DiscardAttempt();
}

GoogleConnectionState GoogleConnection::State() const {
	return state;
}

const QString& GoogleConnection::Email() const {
	return email;
}

bool GoogleConnection::HasSavedConnection() const {
	return QFileInfo::exists(filePath);
}

void GoogleConnection::Load() {
	if (state == GoogleConnectionState::UNAVAILABLE || state == GoogleConnectionState::CONNECTING || state == GoogleConnectionState::CONNECTED || state == GoogleConnectionState::SIGN_IN_REQUIRED) {
		return;
	}
	if (!QFileInfo::exists(filePath)) {
		SetState(GoogleConnectionState::DISCONNECTED);
		return;
	}
	const std::optional<DataKey>& localKey = storageEncryption.CurrentKey();
	if (!localKey.has_value()) {
		SetState(GoogleConnectionState::LOCKED);
		return;
	}
	const std::expected<QJsonObject, StorageError> document = ReadStoredDocument(QFileInfo(filePath), MAX_STATE_DOCUMENT_SIZE, localKey, SealedFileKind::GOOGLE);
	const std::optional<QByteArray> keyBytes = document.has_value() ? DecodeBase64(document->value(CLOUD_KEY_FIELD).toString()) : std::nullopt;
	std::optional<DataKey> savedKey = keyBytes.has_value() ? DataKey::FromBytes(*keyBytes) : std::nullopt;
	const QString refreshToken = document.has_value() ? document->value(REFRESH_TOKEN_FIELD).toString() : QString();
	if (!document.has_value() || document->value(FORMAT_FIELD).toString() != STATE_FORMAT || document->value(VERSION_FIELD).toInt() != STATE_FORMAT_VERSION || !savedKey.has_value() || refreshToken.isEmpty()) {
		qWarning("The saved Google connection does not open; Google can be connected again.");
		SetState(GoogleConnectionState::DISCONNECTED);
		return;
	}
	email = document->value(EMAIL_FIELD).toString();
	vaultId = document->value(VAULT_ID_FIELD).toString();
	vaultFileId = document->value(VAULT_FILE_ID_FIELD).toString();
	cloudKey = std::move(savedKey);
	links = ReadLinks(document->value(LINKS_FIELD).toArray());
	cloudOnlyRecords = ReadCloudOnlyRecords(document->value(CLOUD_ONLY_FIELD).toArray());
	DeleteLater(drive);
	DeleteLater(access);
	access = std::make_unique<GoogleAccess>(googleTransport, googleEndpoints, googleClient, refreshToken);
	connect(access.get(), &GoogleAccess::SignInRequired, this, [this]() {
		SetState(GoogleConnectionState::SIGN_IN_REQUIRED);
	});
	drive = std::make_unique<GoogleDrive>(googleTransport, *access, googleEndpoints);
	SetState(GoogleConnectionState::CONNECTED);
}

void GoogleConnection::Connect() {
	if (state == GoogleConnectionState::UNAVAILABLE || state == GoogleConnectionState::CONNECTING || state == GoogleConnectionState::CONNECTED) {
		return;
	}
	if (!storageEncryption.CurrentKey().has_value()) {
		Q_EMIT ConnectFailed(GoogleConnectFailure::ENCRYPTION_OFF);
		return;
	}
	std::optional<ProofKey> proofKey = GenerateProofKey();
	const std::optional<QString> signInState = GenerateState();
	if (!proofKey.has_value() || !signInState.has_value()) {
		Q_EMIT ConnectFailed(GoogleConnectFailure::KEY_GENERATION_FAILED);
		return;
	}
	auto newAttempt = std::make_unique<SignInAttempt>();
	newAttempt->number = ++attemptCount;
	newAttempt->stateBefore = state;
	newAttempt->proofKey = std::move(*proofKey);
	newAttempt->receiver = std::make_unique<GoogleRedirectReceiver>(*signInState);
	if (!newAttempt->receiver->Listen()) {
		Q_EMIT ConnectFailed(GoogleConnectFailure::RECEIVER_UNAVAILABLE);
		return;
	}
	newAttempt->redirectUri = newAttempt->receiver->RedirectUri();
	const quint64 number = newAttempt->number;
	connect(newAttempt->receiver.get(), &GoogleRedirectReceiver::CodeReceived, this, [this, number](const QString& authorizationCode) {
		if (IsCurrentAttempt(number)) {
			ExchangeCode(authorizationCode);
		}
	});
	connect(newAttempt->receiver.get(), &GoogleRedirectReceiver::SignInRefused, this, [this, number](const QString& reason) {
		if (IsCurrentAttempt(number)) {
			Fail(reason == ACCESS_DENIED_REASON ? GoogleConnectFailure::SIGN_IN_CANCELLED : GoogleConnectFailure::GOOGLE_UNREACHABLE);
		}
	});
	const QUrl pageUrl = AuthorizationUrl(googleEndpoints, googleClient, newAttempt->redirectUri, newAttempt->proofKey, *signInState);
	attempt = std::move(newAttempt);
	SetState(GoogleConnectionState::CONNECTING);
	Q_EMIT SignInPageReady(pageUrl);
}

void GoogleConnection::CancelConnecting() {
	if (attempt == nullptr) {
		return;
	}
	const GoogleConnectionState stateBefore = attempt->stateBefore;
	DiscardAttempt();
	SetState(stateBefore);
}

void GoogleConnection::ConfirmRecoveryKeySaved() {
	if (attempt == nullptr || !attempt->header.has_value() || !attempt->recoveryKey.has_value() || attempt->drive == nullptr) {
		return;
	}
	if (attempt->isReplacingVault) {
		const quint64 number = attempt->number;
		attempt->drive->ListFiles(*this, [this, number](std::expected<std::vector<DriveFile>, GoogleError> files) {
			if (!IsCurrentAttempt(number)) {
				return;
			}
			if (!files.has_value()) {
				Fail(files.error());
				return;
			}
			DeleteOldVault(std::move(*files));
		});
		return;
	}
	UploadVaultHeader();
}

void GoogleConnection::DeleteOldVault(std::vector<DriveFile> remainingFiles) {
	if (remainingFiles.empty()) {
		UploadVaultHeader();
		return;
	}
	// The vault header goes last: a reset cut short leaves a vault that asks for its key, so it can be reset again.
	const auto nextFile = std::ranges::find_if(remainingFiles, [](const DriveFile& file) {
		return !IsVaultFile(file);
	});
	const DriveFile file = nextFile != remainingFiles.end() ? *nextFile : remainingFiles.front();
	std::erase_if(remainingFiles, [&file](const DriveFile& remainingFile) {
		return remainingFile.id == file.id;
	});
	const quint64 number = attempt->number;
	attempt->drive->Delete(file.id, *this, [this, number, filesLeft = std::move(remainingFiles)](const std::expected<void, GoogleError>& deleted) mutable {
		if (!IsCurrentAttempt(number)) {
			return;
		}
		if (!deleted.has_value()) {
			Fail(deleted.error());
			return;
		}
		DeleteOldVault(std::move(filesLeft));
	});
}

void GoogleConnection::UploadVaultHeader() {
	if (attempt == nullptr || !attempt->header.has_value() || attempt->drive == nullptr) {
		return;
	}
	const quint64 number = attempt->number;
	const DriveProperties properties{{KIND_PROPERTY, VAULT_KIND}, {VAULT_ID_PROPERTY, attempt->header->vaultId}};
	attempt->drive->Create(VAULT_FILE_NAME, properties, SerializeVaultHeader(*attempt->header), *this, [this, number](std::expected<DriveFile, GoogleError> uploaded) {
		if (!IsCurrentAttempt(number)) {
			return;
		}
		if (!uploaded.has_value()) {
			Fail(uploaded.error());
			return;
		}
		CheckUploadedVault(*uploaded);
	});
}

void GoogleConnection::ReplaceVault() {
	// Only a vault this computer cannot open: a computer that has the key signs in again instead.
	if (attempt == nullptr || !attempt->header.has_value() || attempt->recoveryKey.has_value() || cloudKey.has_value()) {
		return;
	}
	attempt->isReplacingVault = true;
	CreateVault();
}

void GoogleConnection::OpenVault(const RecoveryKey& recoveryKey) {
	if (attempt == nullptr || !attempt->header.has_value() || attempt->recoveryKey.has_value()) {
		return;
	}
	std::optional<DataKey> openedKey = OpenWithRecoveryKey(*attempt->header, recoveryKey);
	if (!openedKey.has_value()) {
		Q_EMIT RecoveryKeyRejected();
		return;
	}
	attempt->cloudKey = std::move(openedKey);
	FinishConnecting();
}

std::expected<void, StorageError> GoogleConnection::Disconnect() {
	if (const std::expected<void, StorageError> removed = RemoveFileIfExists(filePath); !removed.has_value()) {
		return removed;
	}
	DiscardAttempt();
	DeleteLater(drive);
	DeleteLater(access);
	cloudKey.reset();
	links.clear();
	cloudOnlyRecords.clear();
	email.clear();
	vaultId.clear();
	vaultFileId.clear();
	if (state != GoogleConnectionState::UNAVAILABLE) {
		SetState(GoogleConnectionState::DISCONNECTED);
	}
	return {};
}

GoogleDrive* GoogleConnection::Drive() {
	return state == GoogleConnectionState::CONNECTED ? drive.get() : nullptr;
}

const std::optional<DataKey>& GoogleConnection::CloudKey() const {
	return cloudKey;
}

const QString& GoogleConnection::VaultId() const {
	return vaultId;
}

const std::vector<CloudLink>& GoogleConnection::Links() const {
	return links;
}

std::expected<void, StorageError> GoogleConnection::AddLink(const CloudLink& link) {
	std::vector<CloudLink> changedLinks = links;
	const auto existing = std::ranges::find(changedLinks, link.localRecordId, &CloudLink::localRecordId);
	if (existing != changedLinks.end()) {
		*existing = link;
	} else {
		changedLinks.push_back(link);
	}
	// A record that is here again is no longer only in the cloud.
	std::vector<CloudOnlyRecord> changedCloudOnlyRecords = cloudOnlyRecords;
	std::erase_if(changedCloudOnlyRecords, [&link](const CloudOnlyRecord& record) {
		return record.cloudRecordId == link.cloudRecordId;
	});
	return SaveLinks(std::move(changedLinks), std::move(changedCloudOnlyRecords));
}

std::expected<void, StorageError> GoogleConnection::RemoveLink(const QUuid& localRecordId) {
	std::vector<CloudLink> changedLinks = links;
	std::erase_if(changedLinks, [&localRecordId](const CloudLink& link) {
		return link.localRecordId == localRecordId;
	});
	return SaveLinks(std::move(changedLinks), cloudOnlyRecords);
}

const std::vector<CloudOnlyRecord>& GoogleConnection::CloudOnlyRecords() const {
	return cloudOnlyRecords;
}

std::expected<void, StorageError> GoogleConnection::LeaveOnlyInCloud(const QUuid& localRecordId, const QString& title) {
	std::vector<CloudLink> changedLinks = links;
	const auto link = std::ranges::find(changedLinks, localRecordId, &CloudLink::localRecordId);
	if (link == changedLinks.end()) {
		return {};
	}
	std::vector<CloudOnlyRecord> changedCloudOnlyRecords = cloudOnlyRecords;
	changedCloudOnlyRecords.push_back(CloudOnlyRecord{.cloudRecordId = link->cloudRecordId, .title = title});
	changedLinks.erase(link);
	return SaveLinks(std::move(changedLinks), std::move(changedCloudOnlyRecords));
}

std::expected<void, StorageError> GoogleConnection::ForgetCloudOnlyRecords(const QSet<QString>& cloudRecordIds) {
	std::vector<CloudOnlyRecord> changedCloudOnlyRecords = cloudOnlyRecords;
	const std::size_t forgottenCount = std::erase_if(changedCloudOnlyRecords, [&cloudRecordIds](const CloudOnlyRecord& record) {
		return cloudRecordIds.contains(record.cloudRecordId);
	});
	if (forgottenCount == 0) {
		return {};
	}
	return SaveLinks(links, std::move(changedCloudOnlyRecords));
}

std::expected<void, StorageError> GoogleConnection::SaveLinks(std::vector<CloudLink> changedLinks, std::vector<CloudOnlyRecord> changedCloudOnlyRecords) {
	if (access == nullptr || !cloudKey.has_value()) {
		return std::unexpected(StorageError{.kind = StorageErrorKind::KEY_UNAVAILABLE, .filePath = filePath, .detail = {}});
	}
	const SavedState savedState{.email = email, .refreshToken = access->RefreshToken(), .vaultId = vaultId, .vaultFileId = vaultFileId, .cloudKey = &*cloudKey, .links = &changedLinks, .cloudOnlyRecords = &changedCloudOnlyRecords};
	if (const std::expected<void, StorageError> saved = SaveState(savedState); !saved.has_value()) {
		return saved;
	}
	links = std::move(changedLinks);
	cloudOnlyRecords = std::move(changedCloudOnlyRecords);
	Q_EMIT LinksChanged();
	return {};
}

void GoogleConnection::ExchangeCode(const QString& authorizationCode) {
	const quint64 number = attempt->number;
	googleTransport.Send(CodeExchangeRequest(googleEndpoints, googleClient, authorizationCode, attempt->proofKey, attempt->redirectUri), *this, [this, number](GoogleResponse response) {
		if (!IsCurrentAttempt(number)) {
			return;
		}
		if (!response.has_value()) {
			Fail(response.error());
			return;
		}
		const std::optional<GoogleTokens> tokens = ParseTokenResponse(*response, QDateTime::currentDateTimeUtc());
		if (!tokens.has_value() || tokens->refreshToken.isEmpty()) {
			Fail(GoogleConnectFailure::GOOGLE_UNREACHABLE);
			return;
		}
		if (!GrantsDriveAccess(*tokens)) {
			Fail(GoogleConnectFailure::DRIVE_PERMISSION_MISSING);
			return;
		}
		attempt->email = tokens->email;
		attempt->access = std::make_unique<GoogleAccess>(googleTransport, googleEndpoints, googleClient, tokens->refreshToken);
		attempt->access->UseTokens(*tokens);
		attempt->drive = std::make_unique<GoogleDrive>(googleTransport, *attempt->access, googleEndpoints);
		FindVault();
	});
}

void GoogleConnection::FindVault() {
	const quint64 number = attempt->number;
	attempt->drive->ListFiles(*this, [this, number](std::expected<std::vector<DriveFile>, GoogleError> files) {
		if (!IsCurrentAttempt(number)) {
			return;
		}
		if (!files.has_value()) {
			Fail(files.error());
			return;
		}
		const std::optional<DriveFile> vaultFile = OldestVaultFile(*files);
		if (vaultFile.has_value()) {
			UseExistingVault(*vaultFile);
			return;
		}
		// Signing in again must find the vault this computer already uses.
		if (cloudKey.has_value()) {
			Fail(GoogleConnectFailure::OTHER_VAULT);
			return;
		}
		CreateVault();
	});
}

void GoogleConnection::CreateVault() {
	std::optional<DataKey> newCloudKey = DataKey::Generate();
	std::optional<RecoveryKey> newRecoveryKey = RecoveryKey::Generate();
	if (!newCloudKey.has_value() || !newRecoveryKey.has_value()) {
		Fail(GoogleConnectFailure::KEY_GENERATION_FAILED);
		return;
	}
	std::optional<CloudVaultHeader> header = CreateVaultHeader(QUuid::createUuid().toString(QUuid::WithoutBraces), QDateTime::currentDateTimeUtc(), *newCloudKey, *newRecoveryKey);
	if (!header.has_value()) {
		Fail(GoogleConnectFailure::KEY_GENERATION_FAILED);
		return;
	}
	attempt->header = std::move(header);
	attempt->cloudKey = std::move(newCloudKey);
	attempt->recoveryKey = std::move(newRecoveryKey);
	Q_EMIT NewVaultCreated(attempt->recoveryKey->Text());
}

void GoogleConnection::UseExistingVault(const DriveFile& vaultFile) {
	const quint64 number = attempt->number;
	attempt->drive->Download(vaultFile.id, *this, [this, number, fileId = vaultFile.id](std::expected<QByteArray, GoogleError> headerJson) {
		if (!IsCurrentAttempt(number)) {
			return;
		}
		if (!headerJson.has_value()) {
			Fail(headerJson.error());
			return;
		}
		std::expected<CloudVaultHeader, VaultHeaderError> header = ParseVaultHeader(*headerJson);
		if (!header.has_value()) {
			Fail(HeaderFailure(header.error()));
			return;
		}
		attempt->vaultFileId = fileId;
		if (cloudKey.has_value()) {
			if (header->vaultId != vaultId) {
				Fail(GoogleConnectFailure::OTHER_VAULT);
				return;
			}
			attempt->header = std::move(*header);
			FinishConnecting();
			return;
		}
		attempt->header = std::move(*header);
		attempt->cloudKey.reset();
		attempt->recoveryKey.reset();
		Q_EMIT RecoveryKeyNeeded();
	});
}

void GoogleConnection::CheckUploadedVault(const DriveFile& uploadedFile) {
	const quint64 number = attempt->number;
	attempt->drive->ListFiles(*this, [this, number, uploadedFile](std::expected<std::vector<DriveFile>, GoogleError> files) {
		if (!IsCurrentAttempt(number)) {
			return;
		}
		if (!files.has_value()) {
			Fail(files.error());
			return;
		}
		const std::optional<DriveFile> oldestVault = OldestVaultFile(*files);
		if (!oldestVault.has_value() || oldestVault->id == uploadedFile.id) {
			attempt->vaultFileId = uploadedFile.id;
			FinishConnecting();
			return;
		}
		// Another computer created a vault a moment earlier: its vault stays, this one goes.
		attempt->drive->Delete(uploadedFile.id, *this, [](const std::expected<void, GoogleError>&) {
		});
		UseExistingVault(*oldestVault);
	});
}

void GoogleConnection::FinishConnecting() {
	const std::optional<DataKey>& connectedKey = attempt->cloudKey.has_value() ? attempt->cloudKey : cloudKey;
	if (!connectedKey.has_value() || !attempt->header.has_value() || attempt->access == nullptr) {
		Fail(GoogleConnectFailure::VAULT_DAMAGED);
		return;
	}
	// Links stay only when this computer signs in again to the vault it already uses.
	const bool isSameVault = cloudKey.has_value() && !attempt->cloudKey.has_value() && attempt->header->vaultId == vaultId;
	const std::vector<CloudLink> connectedLinks = isSameVault ? links : std::vector<CloudLink>();
	const std::vector<CloudOnlyRecord> connectedCloudOnlyRecords = isSameVault ? cloudOnlyRecords : std::vector<CloudOnlyRecord>();
	const SavedState savedState{.email = attempt->email, .refreshToken = attempt->access->RefreshToken(), .vaultId = attempt->header->vaultId, .vaultFileId = attempt->vaultFileId, .cloudKey = &*connectedKey, .links = &connectedLinks, .cloudOnlyRecords = &connectedCloudOnlyRecords};
	if (const std::expected<void, StorageError> saved = SaveState(savedState); !saved.has_value()) {
		Fail(GoogleConnectFailure::SAVE_FAILED);
		return;
	}
	links = connectedLinks;
	cloudOnlyRecords = connectedCloudOnlyRecords;
	if (attempt->cloudKey.has_value()) {
		cloudKey = std::move(attempt->cloudKey);
	}
	email = attempt->email;
	vaultId = attempt->header->vaultId;
	vaultFileId = attempt->vaultFileId;
	DeleteLater(drive);
	DeleteLater(access);
	access = std::move(attempt->access);
	drive = std::move(attempt->drive);
	connect(access.get(), &GoogleAccess::SignInRequired, this, [this]() {
		SetState(GoogleConnectionState::SIGN_IN_REQUIRED);
	});
	DiscardAttempt();
	SetState(GoogleConnectionState::CONNECTED);
	Q_EMIT Connected();
}

void GoogleConnection::Fail(GoogleConnectFailure failure) {
	GoogleConnectionState stateBefore = state;
	if (const SignInAttempt* failedAttempt = attempt.get(); failedAttempt != nullptr) {
		stateBefore = failedAttempt->stateBefore;
	}
	DiscardAttempt();
	SetState(stateBefore);
	Q_EMIT ConnectFailed(failure);
}

void GoogleConnection::Fail(const GoogleError& error) {
	Fail(error.kind == GoogleErrorKind::SIGN_IN_REQUIRED ? GoogleConnectFailure::SIGN_IN_CANCELLED : GoogleConnectFailure::GOOGLE_UNREACHABLE);
}

void GoogleConnection::DiscardAttempt() {
	if (attempt == nullptr) {
		return;
	}
	DeleteLater(attempt->drive);
	DeleteLater(attempt->access);
	DeleteLater(attempt->receiver);
	attempt.reset();
}

bool GoogleConnection::IsCurrentAttempt(quint64 number) const {
	return attempt != nullptr && attempt->number == number;
}

void GoogleConnection::SetState(GoogleConnectionState newState) {
	if (state == newState) {
		return;
	}
	state = newState;
	Q_EMIT StateChanged();
}

std::expected<void, StorageError> GoogleConnection::SaveState(const SavedState& savedState) const {
	const std::optional<DataKey>& localKey = storageEncryption.CurrentKey();
	// Without local encryption the cloud key would lie on the disk in the clear.
	if (!localKey.has_value()) {
		return std::unexpected(StorageError{.kind = StorageErrorKind::KEY_UNAVAILABLE, .filePath = filePath, .detail = {}});
	}
	const QJsonObject document{
		{FORMAT_FIELD, STATE_FORMAT},
		{VERSION_FIELD, STATE_FORMAT_VERSION},
		{EMAIL_FIELD, savedState.email},
		{REFRESH_TOKEN_FIELD, savedState.refreshToken},
		{VAULT_ID_FIELD, savedState.vaultId},
		{VAULT_FILE_ID_FIELD, savedState.vaultFileId},
		{CLOUD_KEY_FIELD, QString::fromLatin1(savedState.cloudKey->Bytes().toByteArray().toBase64())},
		{LINKS_FIELD, WriteLinks(*savedState.links)},
		{CLOUD_ONLY_FIELD, WriteCloudOnlyRecords(savedState.cloudOnlyRecords != nullptr ? *savedState.cloudOnlyRecords : std::vector<CloudOnlyRecord>())},
	};
	const std::expected<QByteArray, StorageError> contents = EncodeStoredDocument(document, MAX_STATE_DOCUMENT_SIZE, localKey, SealedFileKind::GOOGLE, filePath);
	if (!contents.has_value()) {
		return std::unexpected(contents.error());
	}
	return WritePrivateFile(filePath, *contents);
}

} // namespace SteamForge
