#include "ui/record_data_dialog.h"

#include "app/account_library.h"
#include "steam/device_id.h"
#include "ui/record_presentation.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	constexpr QLatin1StringView SHOW_SECRET_ICON_RESOURCE{":/styles/eye.svg"};
	constexpr QLatin1StringView HIDE_SECRET_ICON_RESOURCE{":/styles/eye-off.svg"};
	constexpr int MINIMUM_DIALOG_WIDTH = 760;
	constexpr int DIALOG_SPACING = 12;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr int FORM_HORIZONTAL_SPACING = 16;
	constexpr int FORM_VERTICAL_SPACING = 8;
	constexpr int FIELD_BUTTON_SPACING = 8;
	constexpr QLatin1StringView DEVICE_ID_PLACEHOLDER{"android:xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx"};
	constexpr QLatin1StringView REVOCATION_CODE_PLACEHOLDER{"R00000"};
	constexpr int STEAM_ID_ROW = 0;
	constexpr int STEAM_ID_HINT_ROW = 1;
	constexpr int IDENTITY_SECRET_ROW = 2;
	constexpr int IDENTITY_SECRET_HINT_ROW = 3;
	constexpr int DEVICE_ID_ROW = 4;
	constexpr int DEVICE_ID_HINT_ROW = 5;
	constexpr int REVOCATION_CODE_ROW = 6;
	constexpr int REVOCATION_CODE_HINT_ROW = 7;

	QLabel* CreateHintLabel(QWidget* parent) {
		auto* hintLabel = new QLabel(parent);
		hintLabel->setObjectName(SECONDARY_TEXT_STYLE);
		hintLabel->setWordWrap(true);
		return hintLabel;
	}

	void UpdateVisibilityAction(QAction& action, const QLineEdit& edit, const QString& showText, const QString& hideText) {
		const bool isVisible = edit.echoMode() == QLineEdit::Normal;
		action.setIcon(QIcon(isVisible ? HIDE_SECRET_ICON_RESOURCE : SHOW_SECRET_ICON_RESOURCE));
		action.setToolTip(isVisible ? hideText : showText);
	}

	bool HasChanged(const AuthenticatorRecord& savedRecord, const AuthenticatorRecord& updatedRecord) {
		return savedRecord.account.steamId != updatedRecord.account.steamId || savedRecord.secrets.identitySecret != updatedRecord.secrets.identitySecret || savedRecord.secrets.deviceId != updatedRecord.secrets.deviceId || savedRecord.secrets.revocationCode != updatedRecord.secrets.revocationCode;
	}
} // namespace

RecordDataDialog::RecordDataDialog(AccountLibrary& library, AuthenticatorRecord editedRecord, QString title, const std::optional<AccountSession>& session, QWidget* parent) :
	QDialog(parent),
	accountLibrary(library),
	record(std::move(editedRecord)),
	accountTitle(std::move(title)) {
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);
	if (session.has_value()) {
		signedInSteamId = session->steamId;
	}

	introLabel = new QLabel(this);
	introLabel->setWordWrap(true);
	steamIdTitleLabel = new QLabel(this);
	steamIdEdit = new QLineEdit(this);
	steamIdHintLabel = CreateHintLabel(this);
	identitySecretTitleLabel = new QLabel(this);
	identitySecretEdit = new QLineEdit(this);
	identitySecretEdit->setEchoMode(QLineEdit::Password);
	identitySecretVisibilityAction = identitySecretEdit->addAction(QIcon(SHOW_SECRET_ICON_RESOURCE), QLineEdit::TrailingPosition);
	identitySecretHintLabel = CreateHintLabel(this);
	deviceIdTitleLabel = new QLabel(this);
	deviceIdEdit = new QLineEdit(record.secrets.deviceId, this);
	deviceIdEdit->setPlaceholderText(DEVICE_ID_PLACEHOLDER);
	deriveDeviceIdButton = new QPushButton(this);
	// Grows to the height of the field next to it.
	deriveDeviceIdButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
	deviceIdHintLabel = CreateHintLabel(this);
	revocationCodeTitleLabel = new QLabel(this);
	revocationCodeEdit = new QLineEdit(this);
	revocationCodeEdit->setEchoMode(QLineEdit::Password);
	revocationCodeVisibilityAction = revocationCodeEdit->addAction(QIcon(SHOW_SECRET_ICON_RESOURCE), QLineEdit::TrailingPosition);
	// A saved recovery code is neither shown nor replaced here.
	revocationCodeEdit->setReadOnly(!record.secrets.revocationCode.isEmpty());
	revocationCodeVisibilityAction->setVisible(!revocationCodeEdit->isReadOnly());
	revocationCodeHintLabel = CreateHintLabel(this);
	noteLabel = new QLabel(this);
	noteLabel->setObjectName(NOTICE_STYLE);
	noteLabel->setWordWrap(true);
	errorLabel = new QLabel(this);
	errorLabel->setObjectName(ERROR_TEXT_STYLE);
	errorLabel->setWordWrap(true);
	errorLabel->hide();

	if (record.account.steamId.has_value()) {
		steamIdEdit->setText(QString::number(*record.account.steamId));
		steamIdEdit->setReadOnly(true);
	} else if (signedInSteamId.has_value()) {
		steamIdEdit->setText(QString::number(*signedInSteamId));
	}

	auto* formLayout = new QGridLayout();
	formLayout->setHorizontalSpacing(FORM_HORIZONTAL_SPACING);
	formLayout->setVerticalSpacing(FORM_VERTICAL_SPACING);
	formLayout->addWidget(steamIdTitleLabel, STEAM_ID_ROW, 0);
	formLayout->addWidget(steamIdEdit, STEAM_ID_ROW, 1);
	formLayout->addWidget(steamIdHintLabel, STEAM_ID_HINT_ROW, 1);
	formLayout->addWidget(identitySecretTitleLabel, IDENTITY_SECRET_ROW, 0);
	formLayout->addWidget(identitySecretEdit, IDENTITY_SECRET_ROW, 1);
	formLayout->addWidget(identitySecretHintLabel, IDENTITY_SECRET_HINT_ROW, 1);
	formLayout->addWidget(deviceIdTitleLabel, DEVICE_ID_ROW, 0);
	auto* deviceIdLayout = new QHBoxLayout();
	deviceIdLayout->setSpacing(FIELD_BUTTON_SPACING);
	deviceIdLayout->addWidget(deviceIdEdit, 1);
	deviceIdLayout->addWidget(deriveDeviceIdButton);
	formLayout->addLayout(deviceIdLayout, DEVICE_ID_ROW, 1);
	formLayout->addWidget(deviceIdHintLabel, DEVICE_ID_HINT_ROW, 1);
	formLayout->addWidget(revocationCodeTitleLabel, REVOCATION_CODE_ROW, 0);
	formLayout->addWidget(revocationCodeEdit, REVOCATION_CODE_ROW, 1);
	formLayout->addWidget(revocationCodeHintLabel, REVOCATION_CODE_HINT_ROW, 1);
	formLayout->setColumnStretch(1, 1);

	buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
	saveButton = buttonBox->addButton(QString(), QDialogButtonBox::AcceptRole);
	SetButtonRole(*saveButton, ButtonRole::PRIMARY);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(introLabel);
	dialogLayout->addLayout(formLayout);
	dialogLayout->addWidget(noteLabel);
	dialogLayout->addWidget(errorLabel);
	dialogLayout->addWidget(buttonBox);

	connect(identitySecretVisibilityAction, &QAction::triggered, this, [this]() {
		ToggleVisibility(*identitySecretEdit);
	});
	connect(revocationCodeVisibilityAction, &QAction::triggered, this, [this]() {
		ToggleVisibility(*revocationCodeEdit);
	});
	connect(deriveDeviceIdButton, &QPushButton::clicked, this, &RecordDataDialog::DeriveDeviceId);
	connect(steamIdEdit, &QLineEdit::textChanged, this, &RecordDataDialog::UpdateDeriveButton);
	connect(buttonBox, &QDialogButtonBox::accepted, this, &RecordDataDialog::accept);
	connect(buttonBox, &QDialogButtonBox::rejected, this, &RecordDataDialog::reject);

	RetranslateUi();
	UpdateDeriveButton();
}

void RecordDataDialog::accept() {
	const ManualRecordData typedData{
		.steamId = steamIdEdit->isReadOnly() ? QString() : steamIdEdit->text(),
		.identitySecret = identitySecretEdit->text(),
		.deviceId = deviceIdEdit->text(),
		.revocationCode = revocationCodeEdit->isReadOnly() ? QString() : revocationCodeEdit->text(),
	};
	const std::expected<AuthenticatorRecord, ManualRecordDataError> updatedRecord = ApplyManualRecordData(record, typedData);
	if (!updatedRecord.has_value()) {
		switch (updatedRecord.error()) {
			case ManualRecordDataError::INVALID_STEAM_ID:
				errorLabel->setText(tr("The SteamID must be the 17-digit SteamID64 of a user account, for example from the profile link."));
				break;
			case ManualRecordDataError::INVALID_IDENTITY_SECRET:
				errorLabel->setText(tr("The confirmation key must be identity_secret in Base64 as in the .maFile: 28 characters ending with \"=\"."));
				break;
			case ManualRecordDataError::IDENTITY_SECRET_IS_CODE_KEY:
				errorLabel->setText(tr("This is the code key (shared_secret) of the record, not the confirmation key. Take the identity_secret field of the same file."));
				break;
			case ManualRecordDataError::INVALID_DEVICE_ID:
				errorLabel->setText(tr("The device ID must look like android: followed by a UUID, as device_id in the .maFile."));
				break;
			case ManualRecordDataError::INVALID_REVOCATION_CODE:
				errorLabel->setText(tr("The recovery code is R followed by five digits, as revocation_code in the .maFile."));
				break;
		}
		errorLabel->show();
		FitHeightToContents(*this);
		return;
	}
	if (HasChanged(record, *updatedRecord)) {
		if (const std::expected<void, StorageError> saved = accountLibrary.Save(*updatedRecord); !saved.has_value()) {
			QMessageBox::critical(this, windowTitle(), StorageErrorText(saved.error()));
			return;
		}
	}
	QDialog::accept();
}

void RecordDataDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void RecordDataDialog::showEvent(QShowEvent* event) {
	QDialog::showEvent(event);
	FitHeightToContents(*this);
}

void RecordDataDialog::RetranslateUi() {
	setWindowTitle(tr("Add data · %1").arg(accountTitle));
	introLabel->setText(tr("Trade and market confirmations need a Steam sign-in of this account and three values of the same authenticator: the SteamID, the confirmation key (identity_secret) and the device ID (device_id). The easiest way is to import the whole authenticator file again (.maFile or twofactor_<SteamID>.json), and the missing data is added to this record. Leave a field empty to keep what is saved."));
	steamIdTitleLabel->setText(tr("SteamID"));
	identitySecretTitleLabel->setText(tr("Confirmation key"));
	deviceIdTitleLabel->setText(tr("Device ID"));
	deriveDeviceIdButton->setText(tr("Calculate from SteamID"));
	deviceIdHintLabel->setText(tr("For a twofactor_<SteamID>.json file of node-steamcommunity the device ID is calculated from the SteamID. A .maFile of Steam Desktop Authenticator has it as device_id."));
	revocationCodeTitleLabel->setText(tr("Recovery code"));
	revocationCodeEdit->setPlaceholderText(record.secrets.revocationCode.isEmpty() ? REVOCATION_CODE_PLACEHOLDER : tr("Saved"));
	if (record.secrets.revocationCode.isEmpty()) {
		revocationCodeHintLabel->setText(tr("The revocation_code field of the file. It is needed to remove Steam Guard from the account when the codes are lost."));
	} else {
		revocationCodeHintLabel->setText(tr("Saved with the record. It is shown on the account page."));
	}
	UpdateVisibilityAction(*revocationCodeVisibilityAction, *revocationCodeEdit, tr("Show recovery code"), tr("Hide recovery code"));
	if (record.account.steamId.has_value()) {
		steamIdHintLabel->setText(tr("Saved with the record."));
	} else if (signedInSteamId.has_value()) {
		steamIdHintLabel->setText(tr("Steam reported this SteamID when you signed in for this record. Change it only if the record belongs to another account."));
	} else {
		steamIdHintLabel->setText(tr("The 17-digit SteamID64, for example from the profile link."));
	}
	identitySecretEdit->setPlaceholderText(record.secrets.identitySecret.isEmpty() ? tr("identity_secret from the .maFile") : tr("Saved. Enter a new one only to replace it."));
	identitySecretHintLabel->setText(tr("The identity_secret field of the .maFile or twofactor_<SteamID>.json file: 28 characters ending with \"=\". Not shared_secret: that one is the code key."));
	UpdateVisibilityAction(*identitySecretVisibilityAction, *identitySecretEdit, tr("Show key"), tr("Hide key"));
	noteLabel->setText(tr("The code key is not changed here, and a recovery code can only be added when none is saved. A confirmation key of another authenticator does no harm: Steam simply rejects the confirmations."));
	saveButton->setText(tr("Save"));
}

void RecordDataDialog::DeriveDeviceId() {
	if (const std::optional<quint64> steamId = TypedSteamId(); steamId.has_value()) {
		deviceIdEdit->setText(DeriveNodeSteamTotpDeviceId(*steamId));
		deviceIdEdit->setCursorPosition(0);
	}
}

void RecordDataDialog::UpdateDeriveButton() {
	deriveDeviceIdButton->setEnabled(TypedSteamId().has_value());
}

std::optional<quint64> RecordDataDialog::TypedSteamId() const {
	bool isNumber = false;
	const quint64 steamId = steamIdEdit->text().trimmed().toULongLong(&isNumber);
	if (!isNumber || !IsIndividualSteamId(steamId)) {
		return std::nullopt;
	}
	return steamId;
}

void RecordDataDialog::ToggleVisibility(QLineEdit& edit) {
	edit.setEchoMode(edit.echoMode() == QLineEdit::Password ? QLineEdit::Normal : QLineEdit::Password);
	RetranslateUi();
}

} // namespace SteamForge
