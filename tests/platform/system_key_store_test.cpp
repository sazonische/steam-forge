#include "platform/system_key_store.h"

#ifdef Q_OS_LINUX
	#include <QtDBus>
#endif

namespace {
QString NewVaultId() {
	return QStringLiteral("test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QByteArray KeyBytes(const SteamForge::DataKey& key) {
	return key.Bytes().toByteArray();
}

// A failed call shows which failure it was in the test log.
template<typename Value>
QString Outcome(const std::expected<Value, SteamForge::KeyStoreFailure>& result) {
	if (result.has_value()) {
		return QStringLiteral("ok");
	}
	return QStringLiteral("failure %1").arg(static_cast<int>(result.error()));
}

#ifdef Q_OS_LINUX
constexpr QLatin1StringView SECRETS_SERVICE{"org.freedesktop.secrets"};
constexpr QLatin1StringView SECRETS_PATH{"/org/freedesktop/secrets"};
constexpr QLatin1StringView SERVICE_INTERFACE{"org.freedesktop.Secret.Service"};
constexpr QLatin1StringView PROMPT_INTERFACE{"org.freedesktop.Secret.Prompt"};
constexpr QLatin1StringView COLLECTION_INTERFACE{"org.freedesktop.Secret.Collection"};
constexpr QLatin1StringView PROPERTIES_INTERFACE{"org.freedesktop.DBus.Properties"};
constexpr QLatin1StringView LOCKED_PROPERTY{"Locked"};
constexpr QLatin1StringView NO_OBJECT_PATH{"/"};
constexpr std::chrono::milliseconds PROMPT_TIMEOUT{std::chrono::minutes(1)};

QDBusMessage CallSecrets(const QString& path, QLatin1StringView interfaceName, const QString& methodName, const QVariantList& arguments) {
	QDBusMessage call = QDBusMessage::createMethodCall(SECRETS_SERVICE, path, interfaceName, methodName);
	call.setArguments(arguments);
	return QDBusConnection::sessionBus().call(call);
}

// The keyring that holds the key of the vault.
std::optional<QDBusObjectPath> KeyringOf(const QString& vaultId) {
	qDBusRegisterMetaType<QMap<QString, QString>>();
	const QMap<QString, QString> attributes{{QStringLiteral("application"), QStringLiteral("SteamForge")}, {QStringLiteral("steamforge-vault"), vaultId}};
	const QDBusMessage reply = CallSecrets(SECRETS_PATH, SERVICE_INTERFACE, QStringLiteral("SearchItems"), {QVariant::fromValue(attributes)});
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
		return std::nullopt;
	}
	const QList<QDBusObjectPath> items = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0)) + qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
	if (items.isEmpty()) {
		return std::nullopt;
	}
	return QDBusObjectPath(items.front().path().section(QLatin1Char('/'), 0, -2));
}

std::optional<bool> IsKeyringLocked(const QDBusObjectPath& keyring) {
	const QDBusMessage reply = CallSecrets(keyring.path(), PROPERTIES_INTERFACE, QStringLiteral("Get"), {QString(COLLECTION_INTERFACE), QString(LOCKED_PROPERTY)});
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
		return std::nullopt;
	}
	return qdbus_cast<QDBusVariant>(reply.arguments().front()).variant().toBool();
}

// Opens the keyring the way another program would, answering its prompt like the user.
bool UnlockKeyring(const QDBusObjectPath& keyring) {
	const QDBusMessage reply = CallSecrets(SECRETS_PATH, SERVICE_INTERFACE, QStringLiteral("Unlock"), {QVariant::fromValue(QList<QDBusObjectPath>{keyring})});
	if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
		return false;
	}
	const QDBusObjectPath prompt = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
	if (prompt.path() != NO_OBJECT_PATH) {
		QEventLoop loop;
		QDBusConnection::sessionBus().connect(SECRETS_SERVICE, prompt.path(), PROMPT_INTERFACE, QStringLiteral("Completed"), &loop, SLOT(quit()));
		QTimer::singleShot(PROMPT_TIMEOUT, &loop, &QEventLoop::quit);
		if (CallSecrets(prompt.path(), PROMPT_INTERFACE, QStringLiteral("Prompt"), {QString()}).type() != QDBusMessage::ReplyMessage) {
			return false;
		}
		loop.exec();
	}
	return IsKeyringLocked(keyring) == std::optional(false);
}
#endif
} // namespace

class SystemKeyStoreTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void KeepsReturnsAndForgetsTheKey() {
		const QTemporaryDir directory;
		const std::unique_ptr<SteamForge::SystemKeyStore> keyStore = SteamForge::CreateSystemKeyStore(directory.path());
		if (!keyStore->IsAvailable()) {
			QSKIP("This system has no key store, such as Linux without a Secret Service keyring.");
		}
		const QString vaultId = NewVaultId();
		const std::optional<SteamForge::DataKey> key = SteamForge::DataKey::Generate();
		QVERIFY(key.has_value());

		QCOMPARE(Outcome(keyStore->Save(vaultId, *key, SteamForge::KeyProtection::USER_ACCOUNT)), QStringLiteral("ok"));
		const std::expected<SteamForge::DataKey, SteamForge::KeyStoreFailure> loaded = keyStore->Load(vaultId, SteamForge::KeyProtection::USER_ACCOUNT);
		QCOMPARE(Outcome(loaded), QStringLiteral("ok"));
		QCOMPARE(KeyBytes(*loaded), KeyBytes(*key));
		QVERIFY(!keyStore->Load(NewVaultId(), SteamForge::KeyProtection::USER_ACCOUNT).has_value());

		QCOMPARE(Outcome(keyStore->Remove(vaultId, SteamForge::KeyProtection::USER_ACCOUNT)), QStringLiteral("ok"));
		QCOMPARE(keyStore->Load(vaultId, SteamForge::KeyProtection::USER_ACCOUNT).error(), SteamForge::KeyStoreFailure::NOT_FOUND);
	}

	// Platform checks stay inside the tests: moc does not see the platform macros and would drop such slots.
	void RefusesAChangedKeyFile() {
#ifndef Q_OS_WIN
		QSKIP("Only Windows keeps the wrapped key in the data folder.");
#else
		const QTemporaryDir directory;
		const std::unique_ptr<SteamForge::SystemKeyStore> keyStore = SteamForge::CreateSystemKeyStore(directory.path());
		const QString vaultId = NewVaultId();
		const std::optional<SteamForge::DataKey> key = SteamForge::DataKey::Generate();
		QVERIFY(key.has_value());
		QVERIFY(keyStore->Save(vaultId, *key, SteamForge::KeyProtection::USER_ACCOUNT).has_value());

		QFile keyFile(directory.filePath(QStringLiteral("encryption.key")));
		QVERIFY(keyFile.open(QIODevice::ReadOnly));
		QJsonObject keyDocument = QJsonDocument::fromJson(keyFile.readAll()).object();
		keyFile.close();
		QByteArray wrappedKey = QByteArray::fromBase64(keyDocument.value(QStringLiteral("wrappedKey")).toString().toLatin1());
		wrappedKey[wrappedKey.size() / 2] = static_cast<char>(wrappedKey.at(wrappedKey.size() / 2) ^ 1);
		keyDocument.insert(QStringLiteral("wrappedKey"), QString::fromLatin1(wrappedKey.toBase64()));
		QVERIFY(keyFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
		keyFile.write(QJsonDocument(keyDocument).toJson());
		keyFile.close();

		QCOMPARE(keyStore->Load(vaultId, SteamForge::KeyProtection::USER_ACCOUNT).error(), SteamForge::KeyStoreFailure::UNREADABLE);
#endif
	}

	void FindsNoWindowsHelloKeyWithoutAsking() {
#ifndef Q_OS_WIN
		QSKIP("Windows Hello exists only on Windows.");
#else
		const QTemporaryDir directory;
		const std::unique_ptr<SteamForge::SystemKeyStore> keyStore = SteamForge::CreateSystemKeyStore(directory.path());
		QCOMPARE(keyStore->PreferredProtection(), SteamForge::KeyProtection::WINDOWS_HELLO);
		const std::optional<SteamForge::DataKey> key = SteamForge::DataKey::Generate();
		QVERIFY(key.has_value());
		QVERIFY(keyStore->Save(NewVaultId(), *key, SteamForge::KeyProtection::USER_ACCOUNT).has_value());
		// A vault without a Windows Hello key fails before any prompt.
		const std::expected<SteamForge::DataKey, SteamForge::KeyStoreFailure> loaded = keyStore->Load(NewVaultId(), SteamForge::KeyProtection::WINDOWS_HELLO);
		QVERIFY(!loaded.has_value());
		QVERIFY(loaded.error() != SteamForge::KeyStoreFailure::DISMISSED);
		QCOMPARE(keyStore->Save(NewVaultId(), *key, SteamForge::KeyProtection::KEYRING_PASSWORD).error(), SteamForge::KeyStoreFailure::NOT_SET_UP);
#endif
	}

	void KeepsTheKeyInAKeyringOfItsOwn() {
#ifndef Q_OS_LINUX
		QSKIP("Separate keyrings belong to the Secret Service of Linux.");
#else
		// The keyring asks a person to choose and enter the password.
		if (qEnvironmentVariableIsEmpty("STEAMFORGE_TEST_KEYRING_PROMPTS")) {
			QSKIP("Set STEAMFORGE_TEST_KEYRING_PROMPTS when something answers the prompts of the keyring.");
		}
		const QTemporaryDir directory;
		const std::unique_ptr<SteamForge::SystemKeyStore> keyStore = SteamForge::CreateSystemKeyStore(directory.path());
		QVERIFY(keyStore->IsAvailable());
		QCOMPARE(keyStore->PreferredProtection(), SteamForge::KeyProtection::KEYRING_PASSWORD);
		const QString vaultId = NewVaultId();
		const std::optional<SteamForge::DataKey> key = SteamForge::DataKey::Generate();
		QVERIFY(key.has_value());

		QCOMPARE(Outcome(keyStore->Save(vaultId, *key, SteamForge::KeyProtection::KEYRING_PASSWORD)), QStringLiteral("ok"));
		const std::optional<QDBusObjectPath> keyring = KeyringOf(vaultId);
		QVERIFY(keyring.has_value());
		QCOMPARE(IsKeyringLocked(*keyring), std::optional(true));
		for (int i = 0; i < 2; ++i) {
			const std::expected<SteamForge::DataKey, SteamForge::KeyStoreFailure> loaded = keyStore->Load(vaultId, SteamForge::KeyProtection::KEYRING_PASSWORD);
			QCOMPARE(Outcome(loaded), QStringLiteral("ok"));
			QCOMPARE(KeyBytes(*loaded), KeyBytes(*key));
			QCOMPARE(IsKeyringLocked(*keyring), std::optional(true));
		}

		// Left open by another program, the keyring is locked before the key is read, and again after.
		QVERIFY(UnlockKeyring(*keyring));
		const std::expected<SteamForge::DataKey, SteamForge::KeyStoreFailure> loadedFromOpenKeyring = keyStore->Load(vaultId, SteamForge::KeyProtection::KEYRING_PASSWORD);
		QCOMPARE(Outcome(loadedFromOpenKeyring), QStringLiteral("ok"));
		QCOMPARE(KeyBytes(*loadedFromOpenKeyring), KeyBytes(*key));
		QCOMPARE(IsKeyringLocked(*keyring), std::optional(true));

		QCOMPARE(Outcome(keyStore->Remove(vaultId, SteamForge::KeyProtection::KEYRING_PASSWORD)), QStringLiteral("ok"));
		QCOMPARE(keyStore->Load(vaultId, SteamForge::KeyProtection::KEYRING_PASSWORD).error(), SteamForge::KeyStoreFailure::NOT_FOUND);
#endif
	}

	// An open keyring that cannot be told apart from a shared one is not taken for shared: it would be read
	// without its password.
	void ReadsNothingFromAnOpenKeyringItCannotCheck() {
#ifndef Q_OS_LINUX
		QSKIP("Separate keyrings belong to the Secret Service of Linux.");
#else
		if (qEnvironmentVariableIsEmpty("STEAMFORGE_TEST_UNCHECKABLE_KEYRING")) {
			QSKIP("Set STEAMFORGE_TEST_UNCHECKABLE_KEYRING when a stand-in Secret Service keeps the key in an open keyring that does not list its items.");
		}
		const QTemporaryDir directory;
		const std::unique_ptr<SteamForge::SystemKeyStore> keyStore = SteamForge::CreateSystemKeyStore(directory.path());
		QVERIFY(keyStore->IsAvailable());
		const std::expected<SteamForge::DataKey, SteamForge::KeyStoreFailure> loaded = keyStore->Load(NewVaultId(), SteamForge::KeyProtection::KEYRING_PASSWORD);
		QVERIFY2(!loaded.has_value(), "The key was read from a keyring that stayed open.");
		QCOMPARE(loaded.error(), SteamForge::KeyStoreFailure::NOT_SET_UP);
#endif
	}
};

QTEST_MAIN(SystemKeyStoreTest)

#include "system_key_store_test.moc"
