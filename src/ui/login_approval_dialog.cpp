#include "ui/login_approval_dialog.h"

#include "app/account_library.h"
#include "ui/illustration_view.h"
#include "ui/image_resources.h"
#include "ui/record_presentation.h"
#include "ui/widget_styles.h"

namespace SteamForge {

namespace {
	// Time for the desktop to repaint the area behind the hidden windows before the screenshot.
	constexpr std::chrono::milliseconds WINDOW_HIDE_DELAY{400};
	constexpr int MINIMUM_DIALOG_WIDTH = 560;
	constexpr int DIALOG_SPACING = 12;
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	constexpr QMargins CARD_MARGINS{16, 12, 16, 12};
	constexpr QSize ILLUSTRATION_SIZE{360, 190};
	constexpr int DETAILS_HORIZONTAL_SPACING = 16;
	constexpr int DETAILS_VERTICAL_SPACING = 6;
	constexpr int STEAM_CLIENT_PLATFORM = 1;
	constexpr int WEB_BROWSER_PLATFORM = 2;
	constexpr int MOBILE_APP_PLATFORM = 3;
	// HelpWebviewPaths.ChangePassword of the Steam mobile application 3.10.9.
	constexpr QLatin1StringView CHANGE_PASSWORD_URL{"https://help.steampowered.com/wizard/HelpChangePassword"};

	bool CanStartAnother(LoginApprovalStage stage) {
		return stage == LoginApprovalStage::IDLE || stage == LoginApprovalStage::APPROVED || stage == LoginApprovalStage::DENIED || stage == LoginApprovalStage::NOTHING_PENDING || stage == LoginApprovalStage::FAILED || stage == LoginApprovalStage::CANCELLED;
	}

	constexpr int ACCOUNT_ROW = 0;
	constexpr int DEVICE_ROW = 1;
	constexpr int LOCATION_ROW = 2;
	constexpr int IP_ROW = 3;
	constexpr int WARNINGS_ROW = 4;
	constexpr int REMEMBER_ROW = 5;
	constexpr int DETAILS_COLUMN_COUNT = 2;

	// Steam supplies the values, so they are never interpreted as rich text.
	std::pair<QLabel*, QLabel*> AddDetailRow(QGridLayout& layout, QWidget* parent, int row) {
		auto* titleLabel = new QLabel(parent);
		titleLabel->setObjectName(SECONDARY_TEXT_STYLE);
		auto* valueLabel = new QLabel(parent);
		valueLabel->setTextFormat(Qt::PlainText);
		valueLabel->setWordWrap(true);
		valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
		layout.addWidget(titleLabel, row, 0, Qt::AlignTop);
		layout.addWidget(valueLabel, row, 1, Qt::AlignTop);
		return {titleLabel, valueLabel};
	}

	bool CanRetryAfter(LoginApprovalFailureKind failureKind) {
		return failureKind != LoginApprovalFailureKind::SIGN_IN_REQUIRED && failureKind != LoginApprovalFailureKind::MISSING_CODE_KEY;
	}
} // namespace

LoginApprovalDialog::LoginApprovalDialog(AccountLibrary& library, AuthenticatorRecord approvingRecord, QString title, AccountSession approvingSession, QWidget* parent) :
	QDialog(parent),
	accountLibrary(library),
	record(std::move(approvingRecord)),
	accountTitle(std::move(title)),
	session(std::move(approvingSession)) {
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);

	illustration = new IllustrationView(QR_APPROVAL_ILLUSTRATION_RESOURCE, ILLUSTRATION_SIZE, this);
	introLabel = new QLabel(this);
	introLabel->setWordWrap(true);
	statusLabel = new QLabel(this);
	statusLabel->setWordWrap(true);
	statusLabel->setTextFormat(Qt::PlainText);
	detailsCard = new QFrame(this);
	detailsCard->setObjectName(CARD_STYLE);
	auto* detailsLayout = new QGridLayout(detailsCard);
	detailsLayout->setContentsMargins(CARD_MARGINS);
	detailsLayout->setHorizontalSpacing(DETAILS_HORIZONTAL_SPACING);
	detailsLayout->setVerticalSpacing(DETAILS_VERTICAL_SPACING);
	std::tie(accountTitleLabel, accountValueLabel) = AddDetailRow(*detailsLayout, detailsCard, ACCOUNT_ROW);
	std::tie(deviceTitleLabel, deviceValueLabel) = AddDetailRow(*detailsLayout, detailsCard, DEVICE_ROW);
	std::tie(locationTitleLabel, locationValueLabel) = AddDetailRow(*detailsLayout, detailsCard, LOCATION_ROW);
	std::tie(ipTitleLabel, ipValueLabel) = AddDetailRow(*detailsLayout, detailsCard, IP_ROW);
	warningsLabel = new QLabel(detailsCard);
	warningsLabel->setObjectName(ERROR_TEXT_STYLE);
	warningsLabel->setTextFormat(Qt::PlainText);
	warningsLabel->setWordWrap(true);
	detailsLayout->addWidget(warningsLabel, WARNINGS_ROW, 0, 1, DETAILS_COLUMN_COUNT);
	rememberSignInCheckBox = new QCheckBox(detailsCard);
	detailsLayout->addWidget(rememberSignInCheckBox, REMEMBER_ROW, 0, 1, DETAILS_COLUMN_COUNT);
	detailsLayout->setColumnStretch(1, 1);

	approveButton = new QPushButton(this);
	SetButtonRole(*approveButton, ButtonRole::PRIMARY);
	denyButton = new QPushButton(this);
	checkButton = new QPushButton(this);
	findQrButton = new QPushButton(this);
	signInAgainButton = new QPushButton(this);
	SetButtonRole(*signInAgainButton, ButtonRole::PRIMARY);
	changePasswordButton = new QPushButton(this);
	closeButton = new QPushButton(this);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addWidget(signInAgainButton);
	buttonsLayout->addWidget(changePasswordButton);
	buttonsLayout->addWidget(approveButton);
	buttonsLayout->addWidget(denyButton);
	buttonsLayout->addWidget(checkButton);
	buttonsLayout->addWidget(findQrButton);
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(closeButton);

	auto* dialogLayout = new QVBoxLayout(this);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(illustration, 0, Qt::AlignHCenter);
	dialogLayout->addWidget(introLabel);
	dialogLayout->addWidget(detailsCard);
	dialogLayout->addWidget(statusLabel);
	dialogLayout->addLayout(buttonsLayout);

	connect(checkButton, &QPushButton::clicked, this, &LoginApprovalDialog::CheckPendingRequests);
	connect(findQrButton, &QPushButton::clicked, this, &LoginApprovalDialog::FindQrCode);
	connect(approveButton, &QPushButton::clicked, this, [this]() {
		if (approval) {
			approval->Approve(rememberSignInCheckBox->isChecked());
		}
	});
	connect(denyButton, &QPushButton::clicked, this, [this]() {
		if (approval) {
			approval->Deny();
		}
	});
	connect(changePasswordButton, &QPushButton::clicked, this, []() {
		QDesktopServices::openUrl(QUrl(CHANGE_PASSWORD_URL));
	});
	connect(closeButton, &QPushButton::clicked, this, &LoginApprovalDialog::reject);
	connect(signInAgainButton, &QPushButton::clicked, this, [this]() {
		const QUuid recordId = record.id;
		reject();
		Q_EMIT SignInAgainRequested(recordId);
	});
	connect(&screenCapture, &ScreenCapture::Captured, this, &LoginApprovalDialog::HandleCaptured);
	connect(&screenCapture, &ScreenCapture::Failed, this, &LoginApprovalDialog::HandleCaptureFailure);

	RetranslateUi();
}

void LoginApprovalDialog::reject() {
	if (approval) {
		approval->Cancel();
	}
	QDialog::reject();
}

void LoginApprovalDialog::changeEvent(QEvent* event) {
	if (event->type() == QEvent::LanguageChange) {
		RetranslateUi();
	}
	QDialog::changeEvent(event);
}

void LoginApprovalDialog::showEvent(QShowEvent* event) {
	QDialog::showEvent(event);
	if (!hasCheckedOnOpen) {
		hasCheckedOnOpen = true;
		CheckPendingRequests();
	}
}

void LoginApprovalDialog::RetranslateUi() {
	setWindowTitle(tr("Approve a Steam sign-in"));
	introLabel->setText(tr("When Steam on another device asks to approve a sign-in to %1, the request appears here together with who is signing in. To approve a sign-in window that shows a QR code, use \"Find QR code on screen\".").arg(accountTitle));
	approveButton->setText(tr("Approve sign-in"));
	denyButton->setText(tr("Deny"));
	checkButton->setText(tr("Check again"));
	findQrButton->setText(tr("Find QR code on screen"));
	closeButton->setText(tr("Close"));
	signInAgainButton->setText(tr("Sign in to Steam…"));
	changePasswordButton->setText(tr("Change password…"));
	rememberSignInCheckBox->setText(tr("Remember the sign-in on the device that asks"));
	accountTitleLabel->setText(tr("Account"));
	deviceTitleLabel->setText(tr("Device"));
	locationTitleLabel->setText(tr("Location"));
	ipTitleLabel->setText(tr("IP address"));
	UpdateState();
}

void LoginApprovalDialog::StartApproval() {
	captureFailure.reset();
	// A previous attempt may have renewed the tokens, and Steam can revoke the replaced refresh token.
	if (std::optional<AccountSession> savedSession = accountLibrary.FindSession(record.id); savedSession.has_value()) {
		session = std::move(*savedSession);
	}
	approval = std::make_unique<LoginApproval>(transport, accountLibrary, record, session);
	// Preset once per request, so a later repaint keeps what the user chose.
	connect(approval.get(), &LoginApproval::StageChanged, this, [this]() {
		if (approval->Stage() == LoginApprovalStage::AWAITING_DECISION) {
			rememberSignInCheckBox->setChecked(approval->SessionInfo().isPersistenceRequested);
		}
	});
	connect(approval.get(), &LoginApproval::StageChanged, this, &LoginApprovalDialog::UpdateState);
}

void LoginApprovalDialog::CheckPendingRequests() {
	StartApproval();
	approval->CheckPendingRequests();
	UpdateState();
}

void LoginApprovalDialog::FindQrCode() {
	StartApproval();
	isCapturing = true;
	QWidget* mainWindow = parentWidget() != nullptr ? parentWidget()->window() : nullptr;
	wasMainWindowVisible = mainWindow != nullptr && mainWindow->isVisible();
	hide();
	if (wasMainWindowVisible) {
		mainWindow->hide();
	}
	QTimer::singleShot(WINDOW_HIDE_DELAY, this, &LoginApprovalDialog::CaptureScreens);
}

void LoginApprovalDialog::CaptureScreens() {
	screenCapture.Capture();
}

void LoginApprovalDialog::RestoreWindows() {
	isCapturing = false;
	if (wasMainWindowVisible && parentWidget() != nullptr) {
		parentWidget()->window()->show();
	}
	show();
	raise();
	activateWindow();
}

void LoginApprovalDialog::HandleCaptured(const QList<QImage>& images) {
	RestoreWindows();
	if (approval) {
		approval->ProcessScreenImages(images);
	}
	UpdateState();
}

void LoginApprovalDialog::HandleCaptureFailure(ScreenCaptureFailure failureReason) {
	RestoreWindows();
	captureFailure = failureReason;
	approval.reset();
	UpdateState();
}

void LoginApprovalDialog::UpdateState() {
	const LoginApprovalStage stage = approval ? approval->Stage() : LoginApprovalStage::IDLE;
	const bool isAwaitingDecision = stage == LoginApprovalStage::AWAITING_DECISION;
	const bool isRetryPointless = approval && approval->Failure().has_value() && !CanRetryAfter(approval->Failure()->kind);
	const bool canStartAnother = !isCapturing && !isRetryPointless && CanStartAnother(stage);
	approveButton->setVisible(isAwaitingDecision);
	denyButton->setVisible(isAwaitingDecision);
	checkButton->setVisible(canStartAnother);
	findQrButton->setVisible(canStartAnother);
	signInAgainButton->setVisible(approval && approval->Failure().has_value() && approval->Failure()->kind == LoginApprovalFailureKind::SIGN_IN_REQUIRED);
	const bool isPasswordSignInDenied = stage == LoginApprovalStage::DENIED && approval->IsPasswordSignIn();
	changePasswordButton->setVisible(isPasswordSignInDenied);
	rememberSignInCheckBox->setEnabled(isAwaitingDecision);
	detailsCard->setVisible(isAwaitingDecision || stage == LoginApprovalStage::SENDING);
	// The details of a request take the place of the picture, so who asks is seen at once.
	illustration->setVisible(detailsCard->isHidden());
	UpdateDetails();

	const bool isFailure = stage == LoginApprovalStage::FAILED || captureFailure.has_value();
	if (isFailure) {
		SetStyleName(*statusLabel, ERROR_TEXT_STYLE);
	} else if (isPasswordSignInDenied) {
		SetStyleName(*statusLabel, NOTICE_STYLE);
	} else {
		SetStyleName(*statusLabel, SECONDARY_TEXT_STYLE);
	}
	statusLabel->setText(StatusText());
	statusLabel->setVisible(!statusLabel->text().isEmpty());
	FitHeightToContents(*this);
}

QString LoginApprovalDialog::StatusText() const {
	if (captureFailure.has_value()) {
		switch (*captureFailure) {
			case ScreenCaptureFailure::PERMISSION_DENIED:
				return tr("The desktop did not allow a screenshot. Allow it and try again.");
			case ScreenCaptureFailure::PORTAL_UNAVAILABLE:
				return tr("This desktop offers no screenshot service (xdg-desktop-portal).");
			case ScreenCaptureFailure::NOTHING_CAPTURED:
				return tr("The screen could not be captured.");
		}
	}
	if (isCapturing) {
		return tr("Looking for the QR code…");
	}
	if (!approval) {
		return {};
	}
	switch (approval->Stage()) {
		case LoginApprovalStage::IDLE:
			return {};
		case LoginApprovalStage::SEARCHING:
		case LoginApprovalStage::LOADING_DETAILS:
			return tr("Asking Steam for sign-in requests…");
		case LoginApprovalStage::AWAITING_DECISION:
			if (approval->OtherPendingRequestCount() > 0) {
				return tr("Approve only if you started this sign-in yourself. More requests are waiting: %n.", nullptr, static_cast<int>(approval->OtherPendingRequestCount()));
			}
			return tr("Approve only if you started this sign-in yourself.");
		case LoginApprovalStage::SENDING:
			return tr("Sending the decision to Steam…");
		case LoginApprovalStage::APPROVED:
			return tr("Steam accepted the approval. The device that asked finishes signing in by itself.");
		case LoginApprovalStage::DENIED:
			if (approval->IsPasswordSignIn()) {
				return tr("The sign-in was denied. If you did not start it, someone else probably knows the account name and password: change the password now.");
			}
			return tr("The sign-in was denied.");
		case LoginApprovalStage::NOTHING_PENDING:
			return tr("No sign-in is waiting for approval for this account. If the Steam window shows a QR code, use \"Find QR code on screen\".");
		case LoginApprovalStage::CANCELLED:
			return {};
		case LoginApprovalStage::FAILED:
			return approval->Failure().has_value() ? FailureText(*approval->Failure()) : QString();
	}
	return {};
}

void LoginApprovalDialog::UpdateDetails() {
	if (!approval) {
		return;
	}
	const AuthSessionInfo& info = approval->SessionInfo();
	QString platform = tr("Unknown device");
	if (info.platformType == STEAM_CLIENT_PLATFORM) {
		platform = tr("Steam client");
	} else if (info.platformType == WEB_BROWSER_PLATFORM) {
		platform = tr("Web browser");
	} else if (info.platformType == MOBILE_APP_PLATFORM) {
		platform = tr("Steam mobile application");
	}
	QStringList locationParts;
	for (const QString& part : {info.city, info.state, info.country}) {
		if (!part.isEmpty()) {
			locationParts.append(part);
		}
	}

	accountValueLabel->setText(accountTitle);
	deviceValueLabel->setText(info.deviceFriendlyName.isEmpty() ? platform : tr("%1 (%2)").arg(info.deviceFriendlyName, platform));
	locationValueLabel->setText(locationParts.join(QStringLiteral(", ")));
	ipValueLabel->setText(info.ipAddress);
	locationTitleLabel->setVisible(!locationParts.isEmpty());
	locationValueLabel->setVisible(!locationParts.isEmpty());
	ipTitleLabel->setVisible(!info.ipAddress.isEmpty());
	ipValueLabel->setVisible(!info.ipAddress.isEmpty());

	QStringList warnings;
	if (info.isRequestorLocationMismatch) {
		warnings.append(tr("Steam reports that the request comes from another location than this computer."));
	}
	if (info.isHighUsageLogin) {
		warnings.append(tr("Steam reports many recent sign-ins from this address."));
	}
	warningsLabel->setText(warnings.join(QLatin1Char('\n')));
	warningsLabel->setVisible(!warnings.isEmpty());
}

QString LoginApprovalDialog::FailureText(const LoginApprovalFailure& approvalFailure) {
	switch (approvalFailure.kind) {
		case LoginApprovalFailureKind::NO_QR_FOUND:
			return tr("No Steam sign-in QR code was found on the screen. Open the Steam sign-in window and make sure the QR code is fully visible.");
		case LoginApprovalFailureKind::SEVERAL_QR_FOUND:
			return tr("Several Steam sign-in QR codes are on the screen. Leave only the window you want to sign in and try again.");
		case LoginApprovalFailureKind::UNSUPPORTED_VERSION:
			return tr("This sign-in request uses a version that SteamForge does not know. Nothing was sent.");
		case LoginApprovalFailureKind::MISSING_CODE_KEY:
			return tr("This record has no code key, which signs the approval.");
		case LoginApprovalFailureKind::SIGN_IN_REQUIRED:
			return WithErrorCode(tr("Steam no longer accepts the saved sign-in of this account. Sign in to Steam again."), approvalFailure.error);
		case LoginApprovalFailureKind::REQUEST_ENDED:
			return tr("This sign-in request is no longer active. Start the sign-in again and check once more.");
		case LoginApprovalFailureKind::CONNECTION:
			return tr("Steam could not be reached. Check the internet connection and try again.");
		case LoginApprovalFailureKind::STEAM_ERROR:
			return DescribeWebApiError(tr("The sign-in request could not be handled."), approvalFailure.error);
		case LoginApprovalFailureKind::STORAGE_ERROR:
			return tr("The renewed sign-in could not be saved on this computer.");
	}
	return {};
}

} // namespace SteamForge
