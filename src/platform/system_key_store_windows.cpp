#include "platform/system_key_store.h"

#ifdef Q_OS_WIN
	#include "storage/private_file.h"

	#include <windows.h>

	#include <dpapi.h>
	#include <ncrypt.h>
	#include <sddl.h>

namespace SteamForge {

namespace {
	constexpr QLatin1StringView KEY_FILE_NAME{"encryption.key"};
	// The vault ID is mixed in, so the key file of one data folder does not open another.
	constexpr QLatin1StringView ENTROPY_PATTERN{"SteamForge data key %1"};
	constexpr QStringView KEY_DESCRIPTION{u"SteamForge data key"};
	// Windows Hello keys are named "<user SID>//<domain>/<subdomain>/<name>".
	constexpr QLatin1StringView HELLO_KEY_NAME_PATTERN{"%1//SteamForge/DataKey/%2"};
	constexpr DWORD HELLO_KEY_BITS = 2048;
	constexpr DWORD HELLO_WRAPPED_KEY_LENGTH = HELLO_KEY_BITS / 8;
	constexpr DWORD GESTURE_REQUIRED = 1;
	constexpr qint64 MAX_KEY_FILE_SIZE = qint64{64} << 10;
	constexpr QLatin1StringView PROTECTION_KEY{"protection"};
	constexpr QLatin1StringView WRAPPED_KEY_KEY{"wrappedKey"};
	constexpr QLatin1StringView USER_ACCOUNT_NAME{"user-account"};
	constexpr QLatin1StringView WINDOWS_HELLO_NAME{"windows-hello"};

	struct LocalMemoryDeleter {
		void operator()(void* memory) const {
			LocalFree(memory);
		}
	};

	// A CNG provider or key handle, freed when it goes out of scope.
	class NcryptHandle final {
	public:
		NcryptHandle() = default;
		NcryptHandle(const NcryptHandle&) = delete;
		NcryptHandle& operator=(const NcryptHandle&) = delete;

		NcryptHandle(NcryptHandle&& other) noexcept :
			handle(std::exchange(other.handle, 0)) {
		}

		NcryptHandle& operator=(NcryptHandle&& other) noexcept {
			std::swap(handle, other.handle);
			return *this;
		}

		~NcryptHandle() {
			if (handle != 0) {
				NCryptFreeObject(handle);
			}
		}

		[[nodiscard]] NCRYPT_HANDLE* Out() {
			return &handle;
		}

		[[nodiscard]] NCRYPT_HANDLE Get() const {
			return handle;
		}

		// After NCryptDeleteKey, which frees the handle itself.
		void Forget() {
			handle = 0;
		}

	private:
		NCRYPT_HANDLE handle = 0;
	};

	DATA_BLOB Blob(QByteArray& bytes) {
		return DATA_BLOB{
			.cbData = static_cast<DWORD>(bytes.size()),
			.pbData = reinterpret_cast<BYTE*>(bytes.data()),
		};
	}

	// The Windows APIs take writable buffers; the caller wipes the copy.
	QByteArray WritableCopy(QByteArrayView bytes) {
		return bytes.toByteArray();
	}

	std::optional<QString> CurrentUserSid() {
		HANDLE token = nullptr;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token) == FALSE) {
			return std::nullopt;
		}
		const std::unique_ptr<void, decltype(&CloseHandle)> tokenGuard(token, &CloseHandle);
		DWORD length = 0;
		GetTokenInformation(token, TokenUser, nullptr, 0, &length);
		QByteArray userBuffer(static_cast<qsizetype>(length), Qt::Uninitialized);
		if (GetTokenInformation(token, TokenUser, userBuffer.data(), length, &length) == FALSE) {
			return std::nullopt;
		}
		const auto* user = reinterpret_cast<const TOKEN_USER*>(userBuffer.constData());
		LPWSTR sidText = nullptr;
		if (ConvertSidToStringSidW(user->User.Sid, &sidText) == FALSE) {
			return std::nullopt;
		}
		const std::unique_ptr<void, LocalMemoryDeleter> sidGuard(sidText);
		return QString::fromWCharArray(sidText);
	}

	KeyStoreFailure HelloFailure(SECURITY_STATUS status) {
		if (status == NTE_USER_CANCELLED) {
			return KeyStoreFailure::DISMISSED;
		}
		if (status == NTE_BAD_KEYSET || status == NTE_NOT_FOUND) {
			return KeyStoreFailure::NOT_FOUND;
		}
		return KeyStoreFailure::NOT_SET_UP;
	}

	// Windows data protection (DPAPI) for the user account, or a Windows Hello key that asks for the PIN, face or
	// fingerprint every time the data key is unwrapped.
	class WindowsKeyStore final : public SystemKeyStore {
	public:
		explicit WindowsKeyStore(QString keyPath) :
			keyFilePath(std::move(keyPath)) {
		}

		bool IsAvailable() override {
			return true;
		}

		KeyProtection PreferredProtection() const override {
			return KeyProtection::WINDOWS_HELLO;
		}

		void SetPromptWindow(WId window) override {
			// WId is the integer form of the native HWND.
			promptWindow = reinterpret_cast<HWND>(window); // NOLINT(performance-no-int-to-ptr)
		}

		std::expected<void, KeyStoreFailure> Save(const QString& vaultId, const DataKey& key, KeyProtection protection) override {
			if (protection == KeyProtection::KEYRING_PASSWORD) {
				return std::unexpected(KeyStoreFailure::NOT_SET_UP);
			}
			std::expected<QByteArray, KeyStoreFailure> wrapped = protection == KeyProtection::WINDOWS_HELLO ? WrapWithHello(vaultId, key) : WrapWithDpapi(vaultId, key);
			if (!wrapped.has_value()) {
				return std::unexpected(wrapped.error());
			}
			if (protection == KeyProtection::WINDOWS_HELLO) {
				if (const std::expected<void, KeyStoreFailure> confirmed = ConfirmHelloKey(vaultId, *wrapped, key); !confirmed.has_value()) {
					return confirmed;
				}
			}
			const QJsonObject keyFile{
				{PROTECTION_KEY, protection == KeyProtection::WINDOWS_HELLO ? WINDOWS_HELLO_NAME : USER_ACCOUNT_NAME},
				{WRAPPED_KEY_KEY, QString::fromLatin1(wrapped->toBase64())},
			};
			if (!WritePrivateFile(keyFilePath, QJsonDocument(keyFile).toJson(QJsonDocument::Indented)).has_value()) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			return {};
		}

		std::expected<DataKey, KeyStoreFailure> Load(const QString& vaultId, KeyProtection protection) override {
			std::expected<QByteArray, KeyStoreFailure> wrapped = ReadWrappedKey();
			if (!wrapped.has_value()) {
				return std::unexpected(wrapped.error());
			}
			return protection == KeyProtection::WINDOWS_HELLO ? UnwrapWithHello(vaultId, *wrapped) : UnwrapWithDpapi(vaultId, *wrapped);
		}

		std::expected<void, KeyStoreFailure> Remove(const QString& vaultId, KeyProtection protection) override {
			if (protection == KeyProtection::WINDOWS_HELLO) {
				if (const std::expected<void, KeyStoreFailure> deleted = DeleteHelloKey(vaultId); !deleted.has_value() && deleted.error() != KeyStoreFailure::NOT_FOUND) {
					return deleted;
				}
			}
			if (!RemoveFileIfExists(keyFilePath).has_value()) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			return {};
		}

	private:
		std::expected<QByteArray, KeyStoreFailure> ReadWrappedKey() const {
			QFile keyFile(keyFilePath);
			if (!keyFile.exists()) {
				return std::unexpected(KeyStoreFailure::NOT_FOUND);
			}
			if (keyFile.size() > MAX_KEY_FILE_SIZE || !keyFile.open(QIODevice::ReadOnly)) {
				return std::unexpected(KeyStoreFailure::UNREADABLE);
			}
			QByteArray contents = keyFile.readAll();
			const QJsonDocument document = QJsonDocument::fromJson(contents);
			// The first files held the bare DPAPI blob.
			if (!document.isObject()) {
				return contents;
			}
			return QByteArray::fromBase64(document.object().value(WRAPPED_KEY_KEY).toString().toLatin1());
		}

		static std::expected<QByteArray, KeyStoreFailure> WrapWithDpapi(const QString& vaultId, const DataKey& key) {
			QByteArray keyBytes = WritableCopy(key.Bytes());
			QByteArray entropy = QString(ENTROPY_PATTERN).arg(vaultId).toUtf8();
			DATA_BLOB input = Blob(keyBytes);
			DATA_BLOB entropyBlob = Blob(entropy);
			DATA_BLOB output{};
			const bool isProtected = CryptProtectData(&input, reinterpret_cast<LPCWSTR>(KEY_DESCRIPTION.utf16()), &entropyBlob, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) != FALSE;
			SecureZeroMemory(keyBytes.data(), static_cast<SIZE_T>(keyBytes.size()));
			if (!isProtected) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			const std::unique_ptr<void, LocalMemoryDeleter> protectedGuard(output.pbData);
			return QByteArray(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
		}

		static std::expected<DataKey, KeyStoreFailure> UnwrapWithDpapi(const QString& vaultId, QByteArray wrapped) {
			QByteArray entropy = QString(ENTROPY_PATTERN).arg(vaultId).toUtf8();
			DATA_BLOB input = Blob(wrapped);
			DATA_BLOB entropyBlob = Blob(entropy);
			DATA_BLOB output{};
			// Fails for another user account, on another computer, or when the file was changed.
			if (CryptUnprotectData(&input, nullptr, &entropyBlob, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) == FALSE) {
				return std::unexpected(KeyStoreFailure::UNREADABLE);
			}
			const std::unique_ptr<void, LocalMemoryDeleter> keyGuard(output.pbData);
			std::optional<DataKey> key = DataKey::FromBytes(QByteArrayView(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData)));
			SecureZeroMemory(output.pbData, output.cbData);
			if (!key.has_value()) {
				return std::unexpected(KeyStoreFailure::UNREADABLE);
			}
			return std::move(*key);
		}

		static std::expected<std::wstring, KeyStoreFailure> HelloKeyName(const QString& vaultId) {
			const std::optional<QString> sid = CurrentUserSid();
			if (!sid.has_value()) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			return QString(HELLO_KEY_NAME_PATTERN).arg(*sid, vaultId).toStdWString();
		}

		static std::expected<NcryptHandle, KeyStoreFailure> OpenHelloProvider() {
			NcryptHandle provider;
			if (NCryptOpenStorageProvider(provider.Out(), MS_NGC_KEY_STORAGE_PROVIDER, 0) != ERROR_SUCCESS) {
				return std::unexpected(KeyStoreFailure::NOT_SET_UP);
			}
			return provider;
		}

		// The window the Windows Hello prompt belongs to, and the line it shows.
		void ApplyPromptContext(NCRYPT_KEY_HANDLE helloKey) const {
			if (promptWindow != nullptr) {
				HWND window = promptWindow;
				NCryptSetProperty(helloKey, NCRYPT_WINDOW_HANDLE_PROPERTY, reinterpret_cast<PBYTE>(&window), sizeof(HWND), 0);
			}
			std::wstring prompt = QCoreApplication::translate("SteamForge::WindowsKeyStore", "Confirm access to the SteamForge data").toStdWString();
			NCryptSetProperty(helloKey, NCRYPT_USE_CONTEXT_PROPERTY, reinterpret_cast<PBYTE>(prompt.data()), static_cast<DWORD>((prompt.size() + 1) * sizeof(wchar_t)), 0);
		}

		std::expected<QByteArray, KeyStoreFailure> WrapWithHello(const QString& vaultId, const DataKey& key) const {
			const std::expected<std::wstring, KeyStoreFailure> keyName = HelloKeyName(vaultId);
			if (!keyName.has_value()) {
				return std::unexpected(keyName.error());
			}
			std::expected<NcryptHandle, KeyStoreFailure> provider = OpenHelloProvider();
			if (!provider.has_value()) {
				return std::unexpected(provider.error());
			}
			NcryptHandle helloKey;
			if (NCryptCreatePersistedKey(provider->Get(), helloKey.Out(), BCRYPT_RSA_ALGORITHM, keyName->c_str(), 0, NCRYPT_OVERWRITE_KEY_FLAG) != ERROR_SUCCESS) {
				return std::unexpected(KeyStoreFailure::NOT_SET_UP);
			}
			DWORD keyBits = HELLO_KEY_BITS;
			DWORD usage = NCRYPT_ALLOW_DECRYPT_FLAG | NCRYPT_ALLOW_SIGNING_FLAG;
			NCryptSetProperty(helloKey.Get(), NCRYPT_LENGTH_PROPERTY, reinterpret_cast<PBYTE>(&keyBits), sizeof(keyBits), 0);
			NCryptSetProperty(helloKey.Get(), NCRYPT_KEY_USAGE_PROPERTY, reinterpret_cast<PBYTE>(&usage), sizeof(usage), 0);
			ApplyPromptContext(helloKey.Get());
			if (const SECURITY_STATUS status = NCryptFinalizeKey(helloKey.Get(), 0); status != ERROR_SUCCESS) {
				return std::unexpected(HelloFailure(status));
			}
			QByteArray keyBytes = WritableCopy(key.Bytes());
			QByteArray wrapped(static_cast<qsizetype>(HELLO_WRAPPED_KEY_LENGTH), Qt::Uninitialized);
			DWORD wrappedLength = 0;
			const SECURITY_STATUS encrypted = NCryptEncrypt(helloKey.Get(), reinterpret_cast<PBYTE>(keyBytes.data()), static_cast<DWORD>(keyBytes.size()), nullptr, reinterpret_cast<PBYTE>(wrapped.data()), static_cast<DWORD>(wrapped.size()), &wrappedLength, NCRYPT_PAD_PKCS1_FLAG);
			SecureZeroMemory(keyBytes.data(), static_cast<SIZE_T>(keyBytes.size()));
			if (encrypted != ERROR_SUCCESS) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			wrapped.resize(static_cast<qsizetype>(wrappedLength));
			return wrapped;
		}

		std::expected<DataKey, KeyStoreFailure> UnwrapWithHello(const QString& vaultId, QByteArray wrapped) const {
			const std::expected<std::wstring, KeyStoreFailure> keyName = HelloKeyName(vaultId);
			if (!keyName.has_value()) {
				return std::unexpected(keyName.error());
			}
			std::expected<NcryptHandle, KeyStoreFailure> provider = OpenHelloProvider();
			if (!provider.has_value()) {
				return std::unexpected(provider.error());
			}
			NcryptHandle helloKey;
			if (const SECURITY_STATUS status = NCryptOpenKey(provider->Get(), helloKey.Out(), keyName->c_str(), 0, 0); status != ERROR_SUCCESS) {
				return std::unexpected(HelloFailure(status));
			}
			ApplyPromptContext(helloKey.Get());
			DWORD gestureRequired = GESTURE_REQUIRED;
			NCryptSetProperty(helloKey.Get(), NCRYPT_PIN_CACHE_IS_GESTURE_REQUIRED_PROPERTY, reinterpret_cast<PBYTE>(&gestureRequired), sizeof(gestureRequired), 0);
			QByteArray keyBytes(static_cast<qsizetype>(HELLO_WRAPPED_KEY_LENGTH), Qt::Uninitialized);
			DWORD keyLength = 0;
			// Asks for the PIN, face or fingerprint.
			const SECURITY_STATUS decrypted = NCryptDecrypt(helloKey.Get(), reinterpret_cast<PBYTE>(wrapped.data()), static_cast<DWORD>(wrapped.size()), nullptr, reinterpret_cast<PBYTE>(keyBytes.data()), static_cast<DWORD>(keyBytes.size()), &keyLength, NCRYPT_PAD_PKCS1_FLAG);
			if (decrypted != ERROR_SUCCESS) {
				SecureZeroMemory(keyBytes.data(), static_cast<SIZE_T>(keyBytes.size()));
				return std::unexpected(decrypted == NTE_USER_CANCELLED ? KeyStoreFailure::DISMISSED : KeyStoreFailure::UNREADABLE);
			}
			std::optional<DataKey> key = DataKey::FromBytes(QByteArrayView(keyBytes.constData(), static_cast<qsizetype>(keyLength)));
			SecureZeroMemory(keyBytes.data(), static_cast<SIZE_T>(keyBytes.size()));
			if (!key.has_value()) {
				return std::unexpected(KeyStoreFailure::UNREADABLE);
			}
			return std::move(*key);
		}

		// Creating the key may ask nothing, so the PIN, face or fingerprint is asked here once: the user sees what
		// guards the data, and the key provably opens before any file depends on it.
		std::expected<void, KeyStoreFailure> ConfirmHelloKey(const QString& vaultId, const QByteArray& wrapped, const DataKey& key) const {
			const std::expected<DataKey, KeyStoreFailure> unwrapped = UnwrapWithHello(vaultId, wrapped);
			if (unwrapped.has_value() && std::ranges::equal(unwrapped->Bytes(), key.Bytes())) {
				return {};
			}
			if (!DeleteHelloKey(vaultId).has_value()) {
				qWarning("The unused Windows Hello key could not be deleted.");
			}
			return std::unexpected(unwrapped.has_value() ? KeyStoreFailure::FAILED : unwrapped.error());
		}

		static std::expected<void, KeyStoreFailure> DeleteHelloKey(const QString& vaultId) {
			const std::expected<std::wstring, KeyStoreFailure> keyName = HelloKeyName(vaultId);
			if (!keyName.has_value()) {
				return std::unexpected(keyName.error());
			}
			std::expected<NcryptHandle, KeyStoreFailure> provider = OpenHelloProvider();
			if (!provider.has_value()) {
				return std::unexpected(provider.error());
			}
			NcryptHandle helloKey;
			if (const SECURITY_STATUS status = NCryptOpenKey(provider->Get(), helloKey.Out(), keyName->c_str(), 0, 0); status != ERROR_SUCCESS) {
				return std::unexpected(HelloFailure(status));
			}
			// NCryptDeleteKey frees the handle as well.
			if (NCryptDeleteKey(helloKey.Get(), 0) != ERROR_SUCCESS) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			helloKey.Forget();
			return {};
		}

		QString keyFilePath;
		HWND promptWindow = nullptr;
	};
} // namespace

std::unique_ptr<SystemKeyStore> CreateSystemKeyStore(const QString& dataDirectoryPath) {
	return std::make_unique<WindowsKeyStore>(QDir(dataDirectoryPath).filePath(KEY_FILE_NAME));
}

} // namespace SteamForge
#endif
