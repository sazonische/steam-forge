#pragma once

#include "domain/account_session.h"
#include "domain/authenticator_record.h"

namespace SteamForge {

class AccountLibrary;

// Adds the SteamID, the confirmation key and the device ID by hand to a record imported without them.
class RecordDataDialog final : public QDialog {
	Q_OBJECT

public:
	RecordDataDialog(AccountLibrary& library, AuthenticatorRecord editedRecord, QString title, const std::optional<AccountSession>& session, QWidget* parent = nullptr);

	void accept() override;

protected:
	void changeEvent(QEvent* event) override;
	void showEvent(QShowEvent* event) override;

private:
	void RetranslateUi();
	void ToggleVisibility(QLineEdit& edit);
	void DeriveDeviceId();
	void UpdateDeriveButton();
	[[nodiscard]] std::optional<quint64> TypedSteamId() const;

	AccountLibrary& accountLibrary;
	AuthenticatorRecord record;
	// The Steam name, or the account name while Steam has told none.
	QString accountTitle;
	std::optional<quint64> signedInSteamId;

	QLabel* introLabel = nullptr;
	QLabel* steamIdTitleLabel = nullptr;
	QLineEdit* steamIdEdit = nullptr;
	QLabel* steamIdHintLabel = nullptr;
	QLabel* identitySecretTitleLabel = nullptr;
	QLineEdit* identitySecretEdit = nullptr;
	QAction* identitySecretVisibilityAction = nullptr;
	QLabel* identitySecretHintLabel = nullptr;
	QLabel* deviceIdTitleLabel = nullptr;
	QLineEdit* deviceIdEdit = nullptr;
	QPushButton* deriveDeviceIdButton = nullptr;
	QLabel* deviceIdHintLabel = nullptr;
	QLabel* revocationCodeTitleLabel = nullptr;
	QLineEdit* revocationCodeEdit = nullptr;
	QAction* revocationCodeVisibilityAction = nullptr;
	QLabel* revocationCodeHintLabel = nullptr;
	QLabel* noteLabel = nullptr;
	QLabel* errorLabel = nullptr;
	QDialogButtonBox* buttonBox = nullptr;
	QPushButton* saveButton = nullptr;
};

} // namespace SteamForge
