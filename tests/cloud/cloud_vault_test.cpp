#include "cloud/cloud_vault.h"

namespace {

constexpr QLatin1StringView KNOWN_KEY_TEXT{"ABCD-EFGH-IJKL-MNOP-QRST-UVWX-YZ23-4567"};
constexpr QByteArrayView KNOWN_KEY_HEX{"00443214c74254b635cf84653a56d7c675be77df"};

struct VaultFixture {
	SteamForge::DataKey cloudKey;
	SteamForge::RecoveryKey recoveryKey;
	SteamForge::CloudVaultHeader header;
};

std::optional<VaultFixture> NewVault() {
	std::optional<SteamForge::DataKey> cloudKey = SteamForge::DataKey::Generate();
	std::optional<SteamForge::RecoveryKey> recoveryKey = SteamForge::RecoveryKey::Generate();
	if (!cloudKey.has_value() || !recoveryKey.has_value()) {
		return std::nullopt;
	}
	std::optional<SteamForge::CloudVaultHeader> header = SteamForge::CreateVaultHeader(QUuid::createUuid().toString(QUuid::WithoutBraces), QDateTime::currentDateTimeUtc(), *cloudKey, *recoveryKey);
	if (!header.has_value()) {
		return std::nullopt;
	}
	return VaultFixture{.cloudKey = std::move(*cloudKey), .recoveryKey = std::move(*recoveryKey), .header = std::move(*header)};
}

QByteArray WithField(const QByteArray& headerJson, QLatin1StringView field, const QJsonValue& value) {
	QJsonObject object = QJsonDocument::fromJson(headerJson).object();
	object.insert(field, value);
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

class CloudVaultTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void WritesTheRecoveryKeyInGroups() {
		const std::optional<SteamForge::RecoveryKey> key = SteamForge::RecoveryKey::Generate();
		QVERIFY(key.has_value());
		const QString text = key->Text();
		QVERIFY2(QRegularExpression(QStringLiteral("^[A-Z2-7]{4}(-[A-Z2-7]{4}){7}$")).match(text).hasMatch(), qPrintable(text));
		const std::optional<SteamForge::RecoveryKey> parsed = SteamForge::RecoveryKey::Parse(text);
		QVERIFY(parsed.has_value());
		QCOMPARE(parsed->Bytes(), key->Bytes());
	}

	void ReadsTheKeyAsPeopleTypeIt_data() {
		QTest::addColumn<QString>("typedKey");
		QTest::newRow("as shown") << QString(KNOWN_KEY_TEXT);
		QTest::newRow("lower case with spaces") << QStringLiteral("abcd efgh ijkl mnop qrst uvwx yz23 4567");
		QTest::newRow("no separators") << QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
		QTest::newRow("digits for letters") << QStringLiteral("A8CD-EFGH-1JKL-MN0P-QRST-UVWX-YZ23-4567");
	}

	void ReadsTheKeyAsPeopleTypeIt() {
		QFETCH(QString, typedKey);
		const std::optional<SteamForge::RecoveryKey> key = SteamForge::RecoveryKey::Parse(typedKey);
		QVERIFY(key.has_value());
		QCOMPARE(key->Bytes().toByteArray().toHex(), KNOWN_KEY_HEX.toByteArray());
		QCOMPARE(key->Text(), QString(KNOWN_KEY_TEXT));
	}

	void RefusesWhatIsNotAKey_data() {
		QTest::addColumn<QString>("typedKey");
		QTest::newRow("empty") << QString();
		QTest::newRow("one group short") << QStringLiteral("ABCD-EFGH-IJKL-MNOP-QRST-UVWX-YZ23");
		QTest::newRow("one character long") << QStringLiteral("ABCD-EFGH-IJKL-MNOP-QRST-UVWX-YZ23-45677");
		QTest::newRow("not Base32") << QStringLiteral("ABCD-EFGH-IJKL-MNOP-QRST-UVWX-YZ23-4569");
		QTest::newRow("Steam recovery code") << QStringLiteral("R12345");
	}

	void RefusesWhatIsNotAKey() {
		QFETCH(QString, typedKey);
		QVERIFY(!SteamForge::RecoveryKey::Parse(typedKey).has_value());
	}

	void OpensTheVaultWithItsRecoveryKey() {
		const std::optional<VaultFixture> vault = NewVault();
		QVERIFY(vault.has_value());
		const std::expected<SteamForge::CloudVaultHeader, SteamForge::VaultHeaderError> parsed = SteamForge::ParseVaultHeader(SteamForge::SerializeVaultHeader(vault->header));
		QVERIFY(parsed.has_value());
		QCOMPARE(parsed->vaultId, vault->header.vaultId);
		const std::optional<SteamForge::DataKey> opened = SteamForge::OpenWithRecoveryKey(*parsed, vault->recoveryKey);
		QVERIFY(opened.has_value());
		QCOMPARE(opened->Bytes(), vault->cloudKey.Bytes());
	}

	void KeepsTheVaultClosedForAnotherKey() {
		const std::optional<VaultFixture> vault = NewVault();
		const std::optional<SteamForge::RecoveryKey> otherKey = SteamForge::RecoveryKey::Generate();
		QVERIFY(vault.has_value() && otherKey.has_value());
		QVERIFY(!SteamForge::OpenWithRecoveryKey(vault->header, *otherKey).has_value());
	}

	void KeepsASlotMovedToAnotherVaultClosed() {
		const std::optional<VaultFixture> vault = NewVault();
		QVERIFY(vault.has_value());
		const QByteArray moved = WithField(SteamForge::SerializeVaultHeader(vault->header), QLatin1StringView("vaultId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
		const std::expected<SteamForge::CloudVaultHeader, SteamForge::VaultHeaderError> parsed = SteamForge::ParseVaultHeader(moved);
		QVERIFY(parsed.has_value());
		QVERIFY(!SteamForge::OpenWithRecoveryKey(*parsed, vault->recoveryKey).has_value());
	}

	void TellsWhyAHeaderDoesNotOpen() {
		const std::optional<VaultFixture> vault = NewVault();
		QVERIFY(vault.has_value());
		const QByteArray headerJson = SteamForge::SerializeVaultHeader(vault->header);
		QCOMPARE(SteamForge::ParseVaultHeader(QByteArray("{\"name\":\"other\"}")).error(), SteamForge::VaultHeaderError::NOT_A_VAULT);
		QCOMPARE(SteamForge::ParseVaultHeader(QByteArray("not json")).error(), SteamForge::VaultHeaderError::NOT_A_VAULT);
		QCOMPARE(SteamForge::ParseVaultHeader(WithField(headerJson, QLatin1StringView("version"), 2)).error(), SteamForge::VaultHeaderError::NEWER_VERSION);
		QCOMPARE(SteamForge::ParseVaultHeader(WithField(headerJson, QLatin1StringView("vaultId"), QStringLiteral("not an id"))).error(), SteamForge::VaultHeaderError::DAMAGED);
		QCOMPARE(SteamForge::ParseVaultHeader(WithField(headerJson, QLatin1StringView("keySlots"), QJsonArray())).error(), SteamForge::VaultHeaderError::DAMAGED);
	}
};

QTEST_GUILESS_MAIN(CloudVaultTest)

#include "cloud_vault_test.moc"
