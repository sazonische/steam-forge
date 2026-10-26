#include "crypto/data_sealing.h"

namespace {
// RFC 8439, section 2.8.2: the AEAD_CHACHA20_POLY1305 test vector.
constexpr QByteArrayView RFC_KEY_HEX{"808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9f"};
constexpr QByteArrayView RFC_NONCE_HEX{"070000004041424344454647"};
constexpr QByteArrayView RFC_ASSOCIATED_DATA_HEX{"50515253c0c1c2c3c4c5c6c7"};
constexpr QByteArrayView RFC_PLAINTEXT{"Ladies and Gentlemen of the class of '99: If I could offer you only one tip for the future, sunscreen would be it."};
constexpr QByteArrayView RFC_CIPHERTEXT_WITH_TAG_HEX{"d31a8d34648e60db7b86afbc53ef7ec2a4aded51296e08fea9e2b5a736ee62d63dbea45e8ca9671282fafb69da92728b1a71de0a9e060b2905d6a5b67ecd3b3692ddbd7f2d778b8c9803aee328091b58fab324e4fad675945585808b4831d7bc3ff4def08e4b7a9de576d26586cec64b61161ae10b594f09e26a7e902ecbd0600691"};
constexpr QByteArrayView SAMPLE_PLAINTEXT{R"({"sharedSecret":"synthetic"})"};
constexpr QByteArrayView ACCOUNT_PLACE{"steamforge-sealed/1/account/00000000-0000-0000-0000-000000000001"};
constexpr QByteArrayView OTHER_PLACE{"steamforge-sealed/1/account/00000000-0000-0000-0000-000000000002"};
constexpr int SEAL_COUNT = 64;

SteamForge::DataKey Key() {
	return *SteamForge::DataKey::FromBytes(QByteArray::fromHex(RFC_KEY_HEX.toByteArray()));
}
} // namespace

class DataSealingTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void OpensTheVectorOfTheStandard() {
		const SteamForge::SealedData sealed{
			.nonce = QByteArray::fromHex(RFC_NONCE_HEX.toByteArray()),
			.ciphertext = QByteArray::fromHex(RFC_CIPHERTEXT_WITH_TAG_HEX.toByteArray()),
		};
		const std::optional<QByteArray> plaintext = SteamForge::Unseal(Key(), sealed, QByteArray::fromHex(RFC_ASSOCIATED_DATA_HEX.toByteArray()));
		QVERIFY(plaintext.has_value());
		QCOMPARE(*plaintext, RFC_PLAINTEXT.toByteArray());
	}

	void SealsAndOpensWithTheSamePlace() {
		const std::optional<SteamForge::SealedData> sealed = SteamForge::Seal(Key(), SAMPLE_PLAINTEXT, ACCOUNT_PLACE);
		QVERIFY(sealed.has_value());
		QVERIFY(!sealed->ciphertext.contains(SAMPLE_PLAINTEXT.toByteArray()));
		QCOMPARE(SteamForge::Unseal(Key(), *sealed, ACCOUNT_PLACE), std::optional(SAMPLE_PLAINTEXT.toByteArray()));
	}

	void RefusesAnotherPlaceKeyOrChangedByte() {
		const std::optional<SteamForge::SealedData> sealed = SteamForge::Seal(Key(), SAMPLE_PLAINTEXT, ACCOUNT_PLACE);
		QVERIFY(sealed.has_value());
		QVERIFY(!SteamForge::Unseal(Key(), *sealed, OTHER_PLACE).has_value());

		const std::optional<SteamForge::DataKey> otherKey = SteamForge::DataKey::Generate();
		QVERIFY(otherKey.has_value());
		QVERIFY(!SteamForge::Unseal(*otherKey, *sealed, ACCOUNT_PLACE).has_value());

		SteamForge::SealedData changed = *sealed;
		changed.ciphertext[0] = static_cast<char>(changed.ciphertext.at(0) ^ 1);
		QVERIFY(!SteamForge::Unseal(Key(), changed, ACCOUNT_PLACE).has_value());

		SteamForge::SealedData cut = *sealed;
		cut.ciphertext.chop(1);
		QVERIFY(!SteamForge::Unseal(Key(), cut, ACCOUNT_PLACE).has_value());
	}

	void UsesANewNonceEveryTime() {
		QSet<QByteArray> nonces;
		for (int i = 0; i < SEAL_COUNT; ++i) {
			const std::optional<SteamForge::SealedData> sealed = SteamForge::Seal(Key(), SAMPLE_PLAINTEXT, ACCOUNT_PLACE);
			QVERIFY(sealed.has_value());
			nonces.insert(sealed->nonce);
		}
		QCOMPARE(nonces.size(), SEAL_COUNT);
	}

	void AcceptsOnlyKeysOfTheRightLength() {
		QVERIFY(!SteamForge::DataKey::FromBytes(QByteArray(SteamForge::DATA_KEY_LENGTH - 1, 'k')).has_value());
		QVERIFY(!SteamForge::DataKey::FromBytes(QByteArray(SteamForge::DATA_KEY_LENGTH + 1, 'k')).has_value());
		const std::optional<SteamForge::DataKey> first = SteamForge::DataKey::Generate();
		const std::optional<SteamForge::DataKey> second = SteamForge::DataKey::Generate();
		QVERIFY(first.has_value() && second.has_value());
		QVERIFY(first->Bytes() != second->Bytes());
	}
};

QTEST_MAIN(DataSealingTest)

#include "data_sealing_test.moc"
