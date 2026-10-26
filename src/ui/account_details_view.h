#pragma once

#include "app/account_confirmations.h"
#include "domain/account_session.h"
#include "domain/authenticator_record.h"

namespace SteamForge {

class AccountProfiles;
class IllustrationView;
class PictureView;

// Keeps its own copy of the record, so actions always refer to the record that is shown.
class AccountDetailsView final : public QWidget {
	Q_OBJECT

public:
	explicit AccountDetailsView(AccountProfiles& profiles, QWidget* parent = nullptr);

	void ShowRecord(const AuthenticatorRecord& record, const std::optional<AccountSession>& session, const AccountConfirmations* confirmations);
	[[nodiscard]] std::optional<QUuid> ShownRecordId() const;
	// Follows the eye button of the account in the list.
	void SetAccountNameHidden(bool isHidden);

Q_SIGNALS:
	void RemoveRequested(const QUuid& recordId);
	void AddDataRequested(const QUuid& recordId);
	void ExportRequested(const QUuid& recordId);
	void QrCodeRequested(const QUuid& recordId);
	void ImportRequested();
	void SignInRequested(const QUuid& recordId);
	void ApproveSignInRequested(const QUuid& recordId);
	void ConfirmationsRequested(const QUuid& recordId);
	void SignOutRequested(const QUuid& recordId);
	void SetUpGuardRequested(const QUuid& recordId);
	void MoveGuardRequested(const QUuid& recordId);

protected:
	void changeEvent(QEvent* event) override;

private:
	using RecordSignal = void (AccountDetailsView::*)(const QUuid&);

	void EmitForShownRecord(RecordSignal recordSignal);
	void RetranslateUi();
	void UpdateHeader(const AuthenticatorRecord& record);
	void UpdateGuardCard(bool canUseSession);
	void UpdateConfirmationsCard(bool canUseSession);
	void UpdateSetupSteps(bool hasConfirmationData, bool canUseSession);
	void UpdateRestrictionNotice();
	void ToggleRevocationCode();
	[[nodiscard]] bool HidesAccountName() const;
	[[nodiscard]] QString SessionAccountName() const;

	AccountProfiles& accountProfiles;
	std::optional<AuthenticatorRecord> shownRecord;
	// As of the last update, so every card of the page agrees on it.
	QString shownPersonaName;
	bool isAccountNameHidden = true;
	std::optional<AccountSession> shownSession;
	QPointer<const AccountConfirmations> shownConfirmations;
	bool isRevocationCodeVisible = false;

	PictureView* avatarPicture = nullptr;
	QLabel* nameLabel = nullptr;
	QLabel* detailsLabel = nullptr;
	QWidget* restrictionNotice = nullptr;
	QLabel* restrictionLabel = nullptr;
	QPushButton* restrictionHelpButton = nullptr;
	QUrl restrictionHelpUrl;
	QFrame* signInCard = nullptr;
	QLabel* signInSectionLabel = nullptr;
	QLabel* signInStatusLabel = nullptr;
	QPushButton* signInButton = nullptr;
	QPushButton* approveSignInButton = nullptr;
	QPushButton* signOutButton = nullptr;
	QFrame* confirmationsCard = nullptr;
	QLabel* confirmationsSectionLabel = nullptr;
	QLabel* confirmationsSummaryLabel = nullptr;
	QPushButton* openConfirmationsButton = nullptr;
	QPushButton* confirmationsSignInButton = nullptr;
	QWidget* setupSteps = nullptr;
	QLabel* setupIntroLabel = nullptr;
	IllustrationView* dataStepMarker = nullptr;
	QLabel* dataStepTitleLabel = nullptr;
	QLabel* dataStepTextLabel = nullptr;
	QPushButton* importFileButton = nullptr;
	QPushButton* enterDataButton = nullptr;
	IllustrationView* signInStepMarker = nullptr;
	QLabel* signInStepTitleLabel = nullptr;
	QLabel* signInStepTextLabel = nullptr;
	QPushButton* stepSignInButton = nullptr;
	QFrame* guardCard = nullptr;
	QLabel* guardSectionLabel = nullptr;
	QLabel* guardStatusLabel = nullptr;
	QPushButton* setUpGuardButton = nullptr;
	QPushButton* moveGuardButton = nullptr;
	QLabel* capabilitiesSectionLabel = nullptr;
	QLabel* codesTitleLabel = nullptr;
	QLabel* codesStatusLabel = nullptr;
	QLabel* confirmationsTitleLabel = nullptr;
	QLabel* confirmationsStatusLabel = nullptr;
	QLabel* revocationTitleLabel = nullptr;
	QLabel* revocationStatusLabel = nullptr;
	QPushButton* revealRevocationButton = nullptr;
	QPushButton* qrCodeButton = nullptr;
	QPushButton* manageButton = nullptr;
	QMenu* manageMenu = nullptr;
	QAction* addDataAction = nullptr;
	QAction* exportAction = nullptr;
	QAction* qrCodeAction = nullptr;
	QAction* removeAction = nullptr;
};

} // namespace SteamForge
