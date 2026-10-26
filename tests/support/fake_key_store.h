#pragma once

#include "platform/system_key_store.h"

// Keeps keys in memory; a test can make the store missing or make a call fail.
class FakeKeyStore final : public SteamForge::SystemKeyStore {
public:
	bool IsAvailable() override {
		return isAvailable;
	}

	SteamForge::KeyProtection PreferredProtection() const override {
		return preferredProtection;
	}

	std::expected<void, SteamForge::KeyStoreFailure> Save(const QString& vaultId, const SteamForge::DataKey& key, SteamForge::KeyProtection protection) override {
		if (saveFailure.has_value()) {
			return std::unexpected(*saveFailure);
		}
		keys.insert(vaultId, key.Bytes().toByteArray());
		lastProtection = protection;
		return {};
	}

	std::expected<SteamForge::DataKey, SteamForge::KeyStoreFailure> Load(const QString& vaultId, SteamForge::KeyProtection /*protection*/) override {
		++loadCount;
		if (loadFailure.has_value()) {
			return std::unexpected(*loadFailure);
		}
		if (!keys.contains(vaultId)) {
			return std::unexpected(SteamForge::KeyStoreFailure::NOT_FOUND);
		}
		std::optional<SteamForge::DataKey> key = SteamForge::DataKey::FromBytes(keys.value(vaultId));
		if (!key.has_value()) {
			return std::unexpected(SteamForge::KeyStoreFailure::UNREADABLE);
		}
		return std::move(*key);
	}

	std::expected<void, SteamForge::KeyStoreFailure> Remove(const QString& vaultId, SteamForge::KeyProtection /*protection*/) override {
		keys.remove(vaultId);
		return {};
	}

	[[nodiscard]] qsizetype KeyCount() const {
		return keys.size();
	}

	bool isAvailable = true;
	SteamForge::KeyProtection preferredProtection = SteamForge::KeyProtection::USER_ACCOUNT;
	std::optional<SteamForge::KeyStoreFailure> saveFailure;
	// Such as a dismissed prompt of Windows Hello or the keyring.
	std::optional<SteamForge::KeyStoreFailure> loadFailure;
	std::optional<SteamForge::KeyProtection> lastProtection;
	int loadCount = 0;

private:
	QHash<QString, QByteArray> keys;
};
