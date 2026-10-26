#include "platform/system_key_store.h"

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
		for (int i = 0; i < 2; ++i) {
			const std::expected<SteamForge::DataKey, SteamForge::KeyStoreFailure> loaded = keyStore->Load(vaultId, SteamForge::KeyProtection::KEYRING_PASSWORD);
			QCOMPARE(Outcome(loaded), QStringLiteral("ok"));
			QCOMPARE(KeyBytes(*loaded), KeyBytes(*key));
		}

		QCOMPARE(Outcome(keyStore->Remove(vaultId, SteamForge::KeyProtection::KEYRING_PASSWORD)), QStringLiteral("ok"));
		QCOMPARE(keyStore->Load(vaultId, SteamForge::KeyProtection::KEYRING_PASSWORD).error(), SteamForge::KeyStoreFailure::NOT_FOUND);
#endif
	}
};

QTEST_MAIN(SystemKeyStoreTest)

#include "system_key_store_test.moc"
