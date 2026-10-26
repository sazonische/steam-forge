#include "platform/system_key_store.h"

#ifdef Q_OS_LINUX
	#include <QtDBus>

namespace SteamForge {

// The Secret structure of the Secret Service API: (oayays).
struct SecretServiceSecret {
	QDBusObjectPath session;
	QByteArray parameters;
	QByteArray value;
	QString contentType;
};

QDBusArgument& operator<<(QDBusArgument& argument, const SecretServiceSecret& secret) {
	argument.beginStructure();
	argument << secret.session << secret.parameters << secret.value << secret.contentType;
	argument.endStructure();
	return argument;
}

const QDBusArgument& operator>>(const QDBusArgument& argument, SecretServiceSecret& secret) {
	argument.beginStructure();
	argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
	argument.endStructure();
	return argument;
}

} // namespace SteamForge

Q_DECLARE_METATYPE(SteamForge::SecretServiceSecret)

namespace SteamForge {

namespace {
	using SecretAttributes = QMap<QString, QString>;

	constexpr QLatin1StringView SECRETS_SERVICE{"org.freedesktop.secrets"};
	constexpr QLatin1StringView SECRETS_PATH{"/org/freedesktop/secrets"};
	// Collections live at /org/freedesktop/secrets/collection/<name> and their items one level below.
	constexpr QLatin1StringView COLLECTION_PATH_PREFIX{"/org/freedesktop/secrets/collection/"};
	constexpr QLatin1StringView SERVICE_INTERFACE{"org.freedesktop.Secret.Service"};
	constexpr QLatin1StringView COLLECTION_INTERFACE{"org.freedesktop.Secret.Collection"};
	constexpr QLatin1StringView ITEM_INTERFACE{"org.freedesktop.Secret.Item"};
	constexpr QLatin1StringView SESSION_INTERFACE{"org.freedesktop.Secret.Session"};
	constexpr QLatin1StringView PROMPT_INTERFACE{"org.freedesktop.Secret.Prompt"};
	constexpr QLatin1StringView PROPERTIES_INTERFACE{"org.freedesktop.DBus.Properties"};
	constexpr QLatin1StringView COLLECTIONS_PROPERTY{"Collections"};
	constexpr QLatin1StringView ITEMS_PROPERTY{"Items"};
	constexpr QLatin1StringView DEFAULT_ALIAS{"default"};
	// The session bus is private to the user; libsecret also falls back to plain transfer.
	constexpr QLatin1StringView PLAIN_ALGORITHM{"plain"};
	constexpr QLatin1StringView NO_OBJECT_PATH{"/"};
	constexpr QLatin1StringView APPLICATION_ATTRIBUTE{"application"};
	constexpr QLatin1StringView APPLICATION_NAME{"SteamForge"};
	constexpr QLatin1StringView VAULT_ATTRIBUTE{"steamforge-vault"};
	constexpr QLatin1StringView COLLECTION_LABEL_PROPERTY{"org.freedesktop.Secret.Collection.Label"};
	constexpr QLatin1StringView OWN_KEYRING_LABEL{"SteamForge"};
	constexpr QLatin1StringView ITEM_LABEL_PROPERTY{"org.freedesktop.Secret.Item.Label"};
	constexpr QLatin1StringView ITEM_ATTRIBUTES_PROPERTY{"org.freedesktop.Secret.Item.Attributes"};
	constexpr QLatin1StringView ITEM_LABEL{"SteamForge data key"};
	constexpr QLatin1StringView CONTENT_TYPE{"application/octet-stream"};
	constexpr QLatin1StringView COMPLETED_SIGNAL{"Completed"};
	// Time for the user to answer a prompt of the keyring.
	constexpr std::chrono::milliseconds PROMPT_TIMEOUT{std::chrono::minutes(2)};

	struct FoundItems {
		QList<QDBusObjectPath> unlocked;
		QList<QDBusObjectPath> locked;
	};

	// The Secret Service keyring: GNOME Keyring, KWallet or KeePassXC. The key with the password protection lives
	// in a keyring of its own that stays locked between starts, so the system asks for its password each time.
	class SecretServiceKeyStore final : public SystemKeyStore {
	public:
		SecretServiceKeyStore() :
			connection(QDBusConnection::sessionBus()) {
			qDBusRegisterMetaType<SecretServiceSecret>();
			qDBusRegisterMetaType<SecretAttributes>();
		}

		bool IsAvailable() override {
			const QDBusConnectionInterface* busInterface = connection.interface();
			if (!connection.isConnected() || busInterface == nullptr) {
				return false;
			}
			// Keyrings are often started on demand by the bus.
			return busInterface->isServiceRegistered(SECRETS_SERVICE).value() || busInterface->activatableServiceNames().value().contains(SECRETS_SERVICE);
		}

		KeyProtection PreferredProtection() const override {
			return KeyProtection::KEYRING_PASSWORD;
		}

		std::expected<void, KeyStoreFailure> Save(const QString& vaultId, const DataKey& key, KeyProtection protection) override {
			if (!IsAvailable()) {
				return std::unexpected(KeyStoreFailure::UNAVAILABLE);
			}
			if (protection == KeyProtection::WINDOWS_HELLO) {
				return std::unexpected(KeyStoreFailure::NOT_SET_UP);
			}
			if (protection == KeyProtection::KEYRING_PASSWORD) {
				return SaveInOwnKeyring(vaultId, key);
			}
			const std::expected<QDBusObjectPath, KeyStoreFailure> collection = DefaultCollection();
			if (!collection.has_value()) {
				return std::unexpected(collection.error());
			}
			if (const std::expected<void, KeyStoreFailure> unlocked = Unlock({*collection}); !unlocked.has_value()) {
				return unlocked;
			}
			return StoreItem(*collection, vaultId, key);
		}

		std::expected<DataKey, KeyStoreFailure> Load(const QString& vaultId, KeyProtection protection) override {
			if (!IsAvailable()) {
				return std::unexpected(KeyStoreFailure::UNAVAILABLE);
			}
			std::expected<FoundItems, KeyStoreFailure> found = Search(vaultId);
			if (!found.has_value()) {
				return std::unexpected(found.error());
			}
			if (found->unlocked.isEmpty() && found->locked.isEmpty()) {
				return std::unexpected(KeyStoreFailure::NOT_FOUND);
			}
			if (found->unlocked.isEmpty()) {
				if (const std::expected<void, KeyStoreFailure> unlocked = Unlock(found->locked); !unlocked.has_value()) {
					return std::unexpected(unlocked.error());
				}
				found = Search(vaultId);
				if (!found.has_value() || found->unlocked.isEmpty()) {
					return std::unexpected(KeyStoreFailure::DISMISSED);
				}
			}
			std::expected<DataKey, KeyStoreFailure> key = ReadItem(found->unlocked.front());
			if (protection == KeyProtection::KEYRING_PASSWORD) {
				// Locked again right away, so the next start asks for the password even after a crash.
				LockOwnKeyring(found->unlocked.front());
			}
			return key;
		}

		std::expected<void, KeyStoreFailure> Remove(const QString& vaultId, KeyProtection protection) override {
			if (!IsAvailable()) {
				return std::unexpected(KeyStoreFailure::UNAVAILABLE);
			}
			const std::expected<FoundItems, KeyStoreFailure> found = Search(vaultId);
			if (!found.has_value()) {
				return std::unexpected(found.error());
			}
			const QList<QDBusObjectPath> items = found->unlocked + found->locked;
			if (protection == KeyProtection::KEYRING_PASSWORD && !items.isEmpty()) {
				if (const std::optional<QDBusObjectPath> ownKeyring = OwnKeyringHolding(items); ownKeyring.has_value()) {
					// The keyring was made for this key alone; deleting it needs no password.
					return DeleteObject(*ownKeyring, COLLECTION_INTERFACE);
				}
			}
			for (const QDBusObjectPath& item : items) {
				if (const std::expected<void, KeyStoreFailure> deleted = DeleteObject(item, ITEM_INTERFACE); !deleted.has_value()) {
					return deleted;
				}
			}
			return {};
		}

	private:
		static QDBusMessage Method(const QString& path, QLatin1StringView interfaceName, const QString& methodName) {
			return QDBusMessage::createMethodCall(SECRETS_SERVICE, path, interfaceName, methodName);
		}

		static SecretAttributes Attributes(const QString& vaultId) {
			return {
				{APPLICATION_ATTRIBUTE, APPLICATION_NAME},
				{VAULT_ATTRIBUTE, vaultId},
			};
		}

		static std::optional<QDBusObjectPath> CollectionOf(const QDBusObjectPath& item) {
			const QString collectionPath = item.path().section(QLatin1Char('/'), 0, -2);
			if (!collectionPath.startsWith(COLLECTION_PATH_PREFIX) || collectionPath.size() == COLLECTION_PATH_PREFIX.size()) {
				return std::nullopt;
			}
			return QDBusObjectPath(collectionPath);
		}

		std::expected<void, KeyStoreFailure> SaveInOwnKeyring(const QString& vaultId, const DataKey& key) {
			const std::expected<QDBusObjectPath, KeyStoreFailure> collection = CreateOwnKeyring();
			if (!collection.has_value()) {
				return std::unexpected(collection.error());
			}
			if (const std::expected<void, KeyStoreFailure> stored = StoreItem(*collection, vaultId, key); !stored.has_value()) {
				if (!DeleteObject(*collection, COLLECTION_INTERFACE).has_value()) {
					qWarning("The unused SteamForge keyring could not be deleted.");
				}
				return stored;
			}
			Lock(*collection);
			return {};
		}

		// The keyring asks the user to choose the password of the new collection.
		std::expected<QDBusObjectPath, KeyStoreFailure> CreateOwnKeyring() {
			const std::expected<QList<QDBusObjectPath>, KeyStoreFailure> collectionsBefore = Collections();
			if (!collectionsBefore.has_value()) {
				return std::unexpected(collectionsBefore.error());
			}
			QDBusMessage call = Method(SECRETS_PATH, SERVICE_INTERFACE, QStringLiteral("CreateCollection"));
			call << QVariantMap{{COLLECTION_LABEL_PROPERTY, QString(OWN_KEYRING_LABEL)}} << QString();
			const QDBusMessage reply = connection.call(call);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
				// Some keyrings keep a single collection and refuse new ones.
				return std::unexpected(KeyStoreFailure::NOT_SET_UP);
			}
			if (const QDBusObjectPath created = qdbus_cast<QDBusObjectPath>(reply.arguments().at(0)); created.path() != NO_OBJECT_PATH) {
				return created;
			}
			if (const std::expected<void, KeyStoreFailure> prompted = RunPrompt(qdbus_cast<QDBusObjectPath>(reply.arguments().at(1))); !prompted.has_value()) {
				return std::unexpected(prompted.error());
			}
			// The Completed signal carries the path too, but a plain object cannot receive it without moc.
			const std::expected<QList<QDBusObjectPath>, KeyStoreFailure> collectionsAfter = Collections();
			if (!collectionsAfter.has_value()) {
				return std::unexpected(collectionsAfter.error());
			}
			QList<QDBusObjectPath> created;
			for (const QDBusObjectPath& collection : *collectionsAfter) {
				if (!collectionsBefore->contains(collection)) {
					created.append(collection);
				}
			}
			if (created.isEmpty()) {
				return std::unexpected(KeyStoreFailure::DISMISSED);
			}
			if (created.size() > 1) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			return created.front();
		}

		// Only the keyring made for the key: locking the main keyring would make other programs ask for it.
		std::optional<QDBusObjectPath> OwnKeyringHolding(const QList<QDBusObjectPath>& keyItems) {
			const std::optional<QDBusObjectPath> collection = CollectionOf(keyItems.front());
			if (!collection.has_value()) {
				return std::nullopt;
			}
			if (const std::expected<QDBusObjectPath, KeyStoreFailure> defaultCollection = DefaultCollection(); defaultCollection.has_value() && *defaultCollection == *collection) {
				return std::nullopt;
			}
			const std::optional<QList<QDBusObjectPath>> collectionItems = ObjectPathsProperty(*collection, COLLECTION_INTERFACE, ITEMS_PROPERTY);
			if (!collectionItems.has_value()) {
				return std::nullopt;
			}
			for (const QDBusObjectPath& item : *collectionItems) {
				if (!keyItems.contains(item)) {
					return std::nullopt;
				}
			}
			return collection;
		}

		void LockOwnKeyring(const QDBusObjectPath& keyItem) {
			if (const std::optional<QDBusObjectPath> ownKeyring = OwnKeyringHolding({keyItem}); ownKeyring.has_value()) {
				Lock(*ownKeyring);
			}
		}

		// Keyrings lock without asking anything.
		void Lock(const QDBusObjectPath& collection) {
			QDBusMessage call = Method(SECRETS_PATH, SERVICE_INTERFACE, QStringLiteral("Lock"));
			call << QVariant::fromValue(QList<QDBusObjectPath>{collection});
			connection.call(call);
		}

		std::expected<void, KeyStoreFailure> StoreItem(const QDBusObjectPath& collection, const QString& vaultId, const DataKey& key) {
			const std::expected<QDBusObjectPath, KeyStoreFailure> session = OpenSession();
			if (!session.has_value()) {
				return std::unexpected(session.error());
			}
			const QVariantMap properties{
				{ITEM_LABEL_PROPERTY, QString(ITEM_LABEL)},
				{ITEM_ATTRIBUTES_PROPERTY, QVariant::fromValue(Attributes(vaultId))},
			};
			SecretServiceSecret secret{
				.session = *session,
				.parameters = {},
				.value = key.Bytes().toByteArray(),
				.contentType = CONTENT_TYPE,
			};
			QDBusMessage call = Method(collection.path(), COLLECTION_INTERFACE, QStringLiteral("CreateItem"));
			call << properties << QVariant::fromValue(secret) << true;
			const QDBusMessage reply = connection.call(call);
			secret.value.fill('\0');
			CloseSession(*session);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			// A locked collection asks the user first; the item exists only if the user agreed.
			if (const QDBusObjectPath prompt = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1)); prompt.path() != NO_OBJECT_PATH) {
				if (const std::expected<void, KeyStoreFailure> prompted = RunPrompt(prompt); !prompted.has_value()) {
					return prompted;
				}
				const std::expected<FoundItems, KeyStoreFailure> found = Search(vaultId);
				if (!found.has_value() || (found->unlocked.isEmpty() && found->locked.isEmpty())) {
					return std::unexpected(KeyStoreFailure::DISMISSED);
				}
			}
			return {};
		}

		std::expected<DataKey, KeyStoreFailure> ReadItem(const QDBusObjectPath& item) {
			const std::expected<QDBusObjectPath, KeyStoreFailure> session = OpenSession();
			if (!session.has_value()) {
				return std::unexpected(session.error());
			}
			QDBusMessage call = Method(item.path(), ITEM_INTERFACE, QStringLiteral("GetSecret"));
			call << QVariant::fromValue(*session);
			const QDBusMessage reply = connection.call(call);
			CloseSession(*session);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
				return std::unexpected(KeyStoreFailure::UNREADABLE);
			}
			SecretServiceSecret secret = qdbus_cast<SecretServiceSecret>(reply.arguments().front());
			std::optional<DataKey> key = DataKey::FromBytes(secret.value);
			secret.value.fill('\0');
			if (!key.has_value()) {
				return std::unexpected(KeyStoreFailure::UNREADABLE);
			}
			return std::move(*key);
		}

		std::expected<void, KeyStoreFailure> DeleteObject(const QDBusObjectPath& object, QLatin1StringView interfaceName) {
			const QDBusMessage reply = connection.call(Method(object.path(), interfaceName, QStringLiteral("Delete")));
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			if (const QDBusObjectPath prompt = qdbus_cast<QDBusObjectPath>(reply.arguments().front()); prompt.path() != NO_OBJECT_PATH) {
				return RunPrompt(prompt);
			}
			return {};
		}

		std::expected<QDBusObjectPath, KeyStoreFailure> OpenSession() {
			QDBusMessage call = Method(SECRETS_PATH, SERVICE_INTERFACE, QStringLiteral("OpenSession"));
			call << QString(PLAIN_ALGORITHM) << QVariant::fromValue(QDBusVariant(QString()));
			const QDBusMessage reply = connection.call(call);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
				return std::unexpected(KeyStoreFailure::UNAVAILABLE);
			}
			return qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
		}

		void CloseSession(const QDBusObjectPath& session) {
			connection.call(Method(session.path(), SESSION_INTERFACE, QStringLiteral("Close")));
		}

		std::expected<QDBusObjectPath, KeyStoreFailure> DefaultCollection() {
			QDBusMessage call = Method(SECRETS_PATH, SERVICE_INTERFACE, QStringLiteral("ReadAlias"));
			call << QString(DEFAULT_ALIAS);
			const QDBusMessage reply = connection.call(call);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
				return std::unexpected(KeyStoreFailure::UNAVAILABLE);
			}
			const QDBusObjectPath collection = qdbus_cast<QDBusObjectPath>(reply.arguments().front());
			// Without a default keyring there is nowhere to keep the key.
			if (collection.path() == NO_OBJECT_PATH) {
				return std::unexpected(KeyStoreFailure::UNAVAILABLE);
			}
			return collection;
		}

		std::expected<QList<QDBusObjectPath>, KeyStoreFailure> Collections() {
			std::optional<QList<QDBusObjectPath>> collections = ObjectPathsProperty(QDBusObjectPath(SECRETS_PATH), SERVICE_INTERFACE, COLLECTIONS_PROPERTY);
			if (!collections.has_value()) {
				return std::unexpected(KeyStoreFailure::UNAVAILABLE);
			}
			return std::move(*collections);
		}

		std::optional<QList<QDBusObjectPath>> ObjectPathsProperty(const QDBusObjectPath& object, QLatin1StringView interfaceName, QLatin1StringView propertyName) {
			QDBusMessage call = QDBusMessage::createMethodCall(SECRETS_SERVICE, object.path(), PROPERTIES_INTERFACE, QStringLiteral("Get"));
			call << QString(interfaceName) << QString(propertyName);
			const QDBusMessage reply = connection.call(call);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
				return std::nullopt;
			}
			return qdbus_cast<QList<QDBusObjectPath>>(qdbus_cast<QDBusVariant>(reply.arguments().front()).variant());
		}

		std::expected<FoundItems, KeyStoreFailure> Search(const QString& vaultId) {
			QDBusMessage call = Method(SECRETS_PATH, SERVICE_INTERFACE, QStringLiteral("SearchItems"));
			call << QVariant::fromValue(Attributes(vaultId));
			const QDBusMessage reply = connection.call(call);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			return FoundItems{
				.unlocked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0)),
				.locked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1)),
			};
		}

		std::expected<void, KeyStoreFailure> Unlock(const QList<QDBusObjectPath>& objects) {
			QDBusMessage call = Method(SECRETS_PATH, SERVICE_INTERFACE, QStringLiteral("Unlock"));
			call << QVariant::fromValue(objects);
			const QDBusMessage reply = connection.call(call);
			if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			const QDBusObjectPath prompt = qdbus_cast<QDBusObjectPath>(reply.arguments().at(1));
			if (prompt.path() == NO_OBJECT_PATH) {
				return {};
			}
			return RunPrompt(prompt);
		}

		// Shows the prompt of the keyring and waits for the user; the caller checks the outcome afterwards.
		std::expected<void, KeyStoreFailure> RunPrompt(const QDBusObjectPath& prompt) {
			QEventLoop loop;
			bool isTimedOut = false;
			connection.connect(SECRETS_SERVICE, prompt.path(), PROMPT_INTERFACE, COMPLETED_SIGNAL, &loop, SLOT(quit()));
			QTimer::singleShot(PROMPT_TIMEOUT, &loop, [&loop, &isTimedOut]() {
				isTimedOut = true;
				loop.quit();
			});
			QDBusMessage call = Method(prompt.path(), PROMPT_INTERFACE, QStringLiteral("Prompt"));
			call << QString();
			const QDBusMessage reply = connection.call(call);
			if (reply.type() == QDBusMessage::ReplyMessage) {
				// The window keeps painting, while clicks wait until the prompt is answered.
				loop.exec(QEventLoop::ExcludeUserInputEvents);
			}
			connection.disconnect(SECRETS_SERVICE, prompt.path(), PROMPT_INTERFACE, COMPLETED_SIGNAL, &loop, SLOT(quit()));
			if (reply.type() != QDBusMessage::ReplyMessage) {
				return std::unexpected(KeyStoreFailure::FAILED);
			}
			if (isTimedOut) {
				connection.call(Method(prompt.path(), PROMPT_INTERFACE, QStringLiteral("Dismiss")));
				return std::unexpected(KeyStoreFailure::DISMISSED);
			}
			return {};
		}

		QDBusConnection connection;
	};
} // namespace

std::unique_ptr<SystemKeyStore> CreateSystemKeyStore(const QString& /*dataDirectoryPath*/) {
	return std::make_unique<SecretServiceKeyStore>();
}

} // namespace SteamForge
#endif
