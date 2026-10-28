#include "ui/steam_guard_qr_dialog.h"

#include "ui/image_resources.h"
#include "ui/widget_styles.h"

#include <ZXing/BitMatrix.h>
#include <ZXing/MultiFormatWriter.h>

namespace SteamForge {

namespace {
	constexpr int QUIET_ZONE_MODULES = 4;
	// The same for both links, large for a phone camera, and the window with the app tabs still fits a small laptop.
	constexpr int QR_EDGE_LENGTH = 340;
	// Shrunk to so few pixels, the hidden picture keeps nothing a camera could read back.
	constexpr int HIDDEN_QR_PIXELS = 12;
	constexpr int MINIMUM_DIALOG_WIDTH = 440;
	constexpr int DIALOG_SPACING = 12;
	constexpr int COVER_SPACING = 12;
	constexpr QSize COVER_ICON_SIZE{32, 32};
	constexpr QMargins DIALOG_MARGINS{24, 20, 24, 20};
	// The blurred code dims evenly and darkens further towards the edges, so the icon and button in the middle
	// stand out without a panel behind them.
	constexpr QColor SHADE_COLOR{14, 20, 27};
	constexpr int SHADE_ALPHA = 90;
	constexpr int VIGNETTE_CENTER_ALPHA = 40;
	constexpr int VIGNETTE_EDGE_ALPHA = 220;
	constexpr qreal VIGNETTE_RADIUS = 0.75;
	constexpr std::chrono::milliseconds COPIED_FEEDBACK_DURATION{1500};
	constexpr QColor QR_BACKGROUND{0xff, 0xff, 0xff};
	constexpr QColor QR_MODULE_COLOR{0x00, 0x00, 0x00};
	constexpr int STEAM_ENCODER_LINK_INDEX = 0;
	constexpr int STEAM_TYPE_LINK_INDEX = 1;
	constexpr int APP_TABS_SPACING = 4;

	QImage HiddenQrImage(const QImage& qrImage) {
		QImage blurred = qrImage.scaled(HIDDEN_QR_PIXELS, HIDDEN_QR_PIXELS, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		// Doubling step by step blends the few pixels softly instead of into visible squares.
		while (blurred.width() * 2 < qrImage.width()) {
			blurred = blurred.scaled(blurred.size() * 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
		}
		blurred = blurred.scaled(qrImage.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

		QPainter painter(&blurred);
		QColor shade = SHADE_COLOR;
		shade.setAlpha(SHADE_ALPHA);
		painter.fillRect(blurred.rect(), shade);
		QRadialGradient vignette(blurred.rect().center(), blurred.width() * VIGNETTE_RADIUS);
		shade.setAlpha(VIGNETTE_CENTER_ALPHA);
		vignette.setColorAt(0.0, shade);
		shade.setAlpha(VIGNETTE_EDGE_ALPHA);
		vignette.setColorAt(1.0, shade);
		painter.fillRect(blurred.rect(), vignette);
		return blurred;
	}
} // namespace

QImage QrCodeImage(const QString& text, int edgeLength) {
	ZXing::MultiFormatWriter writer(ZXing::BarcodeFormat::QRCode);
	writer.setMargin(QUIET_ZONE_MODULES);
	const ZXing::BitMatrix matrix = writer.encode(text.toStdString(), 0, 0);
	// Whole pixels per module keep the edges sharp for the camera.
	const int moduleSize = std::max(1, edgeLength / matrix.width());
	const int offset = std::max(0, (edgeLength - matrix.width() * moduleSize) / 2);
	QImage image(std::max(edgeLength, matrix.width()), std::max(edgeLength, matrix.height()), QImage::Format_RGB32);
	image.fill(QR_BACKGROUND);
	QPainter painter(&image);
	for (int row = 0; row < matrix.height(); ++row) {
		for (int column = 0; column < matrix.width(); ++column) {
			if (matrix.get(column, row)) {
				painter.fillRect(offset + (column * moduleSize), offset + (row * moduleSize), moduleSize, moduleSize, QR_MODULE_COLOR);
			}
		}
	}
	return image;
}

SteamGuardQrDialog::SteamGuardQrDialog(const QString& accountTitle, SteamGuardLinks steamGuardLinks, QWidget* parent) :
	QDialog(parent),
	links{std::move(steamGuardLinks.steamEncoderLink), std::move(steamGuardLinks.steamTypeLink)} {
	setWindowTitle(tr("QR code of Steam Guard"));
	setMinimumWidth(MINIMUM_DIALOG_WIDTH);

	auto* warningLabel = new QLabel(tr("With this code anyone gets the Steam Guard codes of %1. Show it only to your own authenticator app.").arg(accountTitle), this);
	warningLabel->setObjectName(NOTICE_STYLE);
	warningLabel->setTextFormat(Qt::PlainText);
	warningLabel->setWordWrap(true);

	auto* appLabel = new QLabel(tr("The app you add the account to:"), this);
	appLabel->setObjectName(SECONDARY_TEXT_STYLE);
	auto* encoderAppsTab = new QPushButton(tr("Stratum, 2FAS for iPhone, KeePassDX"), this);
	auto* steamTypeAppsTab = new QPushButton(tr("Aegis, Ente, 2FAS for Android"), this);
	auto* appTabsLayout = new QHBoxLayout();
	appTabsLayout->setSpacing(APP_TABS_SPACING);
	for (QPushButton* appTab : {encoderAppsTab, steamTypeAppsTab}) {
		appTab->setObjectName(FILTER_TAB_STYLE);
		appTab->setCheckable(true);
		appTab->setCursor(Qt::PointingHandCursor);
		appTabsLayout->addWidget(appTab);
	}
	appTabsLayout->addStretch();
	appGroup.addButton(encoderAppsTab, STEAM_ENCODER_LINK_INDEX);
	appGroup.addButton(steamTypeAppsTab, STEAM_TYPE_LINK_INDEX);
	encoderAppsTab->setChecked(true);

	// No card around it: the white border of the code itself frames it.
	auto* qrArea = new QWidget(this);
	qrLabel = new QLabel(qrArea);
	qrLabel->setAlignment(Qt::AlignCenter);
	qrLabel->setFixedSize(QR_EDGE_LENGTH, QR_EDGE_LENGTH);

	qrCover = new QWidget(qrArea);
	auto* coverIconLabel = new QLabel(qrCover);
	coverIconLabel->setPixmap(QIcon(EYE_OFF_ICON_RESOURCE).pixmap(COVER_ICON_SIZE, devicePixelRatioF()));
	coverIconLabel->setAlignment(Qt::AlignCenter);
	showCodeButton = new QPushButton(tr("Show QR code"), qrCover);
	showCodeButton->setObjectName(QR_REVEAL_BUTTON_STYLE);
	showCodeButton->setCursor(Qt::PointingHandCursor);
	auto* coverLayout = new QVBoxLayout(qrCover);
	coverLayout->setContentsMargins(0, 0, 0, 0);
	coverLayout->setSpacing(COVER_SPACING);
	coverLayout->addWidget(coverIconLabel, 0, Qt::AlignHCenter);
	coverLayout->addWidget(showCodeButton, 0, Qt::AlignHCenter);

	auto* qrAreaLayout = new QGridLayout(qrArea);
	qrAreaLayout->setContentsMargins(0, 0, 0, 0);
	// The icon and the button lie over the blurred code in the same cell.
	qrAreaLayout->addWidget(qrLabel, 0, 0);
	qrAreaLayout->addWidget(qrCover, 0, 0, Qt::AlignCenter);

	hideCodeButton = new QPushButton(QIcon(EYE_OFF_ICON_RESOURCE), tr("Hide QR code"), this);
	hideCodeButton->setObjectName(LINK_BUTTON_STYLE);
	// Keeps its place while hidden, so the window does not jump when the code shows.
	QSizePolicy hideButtonPolicy = hideCodeButton->sizePolicy();
	hideButtonPolicy.setRetainSizeWhenHidden(true);
	hideCodeButton->setSizePolicy(hideButtonPolicy);

	auto* hintLabel = new QLabel(tr("The app must support Steam Guard. Check that it shows the same five-character code as SteamForge; if it does not, try the other tab."), this);
	hintLabel->setObjectName(SECONDARY_TEXT_STYLE);
	hintLabel->setWordWrap(true);

	copyImageButton = new QPushButton(tr("Copy QR code"), this);
	copyLinkButton = new QPushButton(tr("Copy link"), this);
	auto* closeButton = new QPushButton(tr("Close"), this);
	auto* buttonsLayout = new QHBoxLayout();
	buttonsLayout->addWidget(copyImageButton);
	buttonsLayout->addWidget(copyLinkButton);
	buttonsLayout->addStretch();
	buttonsLayout->addWidget(closeButton);

	auto* dialogLayout = new QVBoxLayout(this);
	// An explicit minimum width would stop Qt from growing the window to the app tabs and the code.
	dialogLayout->setSizeConstraint(QLayout::SetMinimumSize);
	dialogLayout->setContentsMargins(DIALOG_MARGINS);
	dialogLayout->setSpacing(DIALOG_SPACING);
	dialogLayout->addWidget(warningLabel);
	dialogLayout->addWidget(appLabel);
	dialogLayout->addLayout(appTabsLayout);
	dialogLayout->addWidget(qrArea, 0, Qt::AlignHCenter);
	dialogLayout->addWidget(hideCodeButton, 0, Qt::AlignHCenter);
	dialogLayout->addWidget(hintLabel);
	dialogLayout->addLayout(buttonsLayout);

	copiedFeedbackTimer.setSingleShot(true);
	copiedFeedbackTimer.setInterval(COPIED_FEEDBACK_DURATION);
	connect(&copiedFeedbackTimer, &QTimer::timeout, this, [this]() {
		copyImageButton->setText(tr("Copy QR code"));
		copyLinkButton->setText(tr("Copy link"));
	});
	connect(copyImageButton, &QPushButton::clicked, this, [this]() {
		QGuiApplication::clipboard()->setImage(qrImage);
		ShowCopied(*copyImageButton, tr("QR code copied"));
	});
	connect(copyLinkButton, &QPushButton::clicked, this, [this]() {
		QGuiApplication::clipboard()->setText(uri);
		ShowCopied(*copyLinkButton, tr("Link copied"));
	});
	connect(closeButton, &QPushButton::clicked, this, &SteamGuardQrDialog::accept);
	connect(showCodeButton, &QPushButton::clicked, this, [this]() {
		SetCodeShown(true);
	});
	connect(hideCodeButton, &QPushButton::clicked, this, [this]() {
		SetCodeShown(false);
	});
	connect(&appGroup, &QButtonGroup::idClicked, this, &SteamGuardQrDialog::SelectLink);
	SelectLink(STEAM_ENCODER_LINK_INDEX);
	FitHeightToContents(*this);
}

void SteamGuardQrDialog::showEvent(QShowEvent* event) {
	QDialog::showEvent(event);
	// The wrapped texts need their height at the width the app tabs gave the window.
	FitHeightToContents(*this);
}

void SteamGuardQrDialog::SelectLink(int linkIndex) {
	uri = links.at(static_cast<std::size_t>(linkIndex));
	const qreal pixelRatio = devicePixelRatioF();
	qrImage = QrCodeImage(uri, qRound(QR_EDGE_LENGTH * pixelRatio));
	qrImage.setDevicePixelRatio(pixelRatio);
	QImage hiddenQrImage = HiddenQrImage(qrImage);
	hiddenQrImage.setDevicePixelRatio(pixelRatio);
	shownQrPixmap = QPixmap::fromImage(qrImage);
	hiddenQrPixmap = QPixmap::fromImage(hiddenQrImage);
	SetCodeShown(isCodeShown);
	FitHeightToContents(*this);
}

void SteamGuardQrDialog::SetCodeShown(bool isShown) {
	isCodeShown = isShown;
	qrLabel->setPixmap(isShown ? shownQrPixmap : hiddenQrPixmap);
	qrLabel->setAccessibleName(isShown ? tr("QR code of Steam Guard") : tr("Hidden QR code"));
	qrCover->setVisible(!isShown);
	hideCodeButton->setVisible(isShown);
	if (isShown) {
		hideCodeButton->setFocus();
	} else {
		showCodeButton->setFocus();
	}
}

void SteamGuardQrDialog::ShowCopied(QPushButton& button, const QString& text) {
	copyImageButton->setText(tr("Copy QR code"));
	copyLinkButton->setText(tr("Copy link"));
	button.setText(text);
	copiedFeedbackTimer.start();
}

} // namespace SteamForge
