#pragma once

namespace SteamForge {

// Black modules on white with the quiet zone the QR standard asks for, so phone cameras read it at once.
[[nodiscard]] QImage QrCodeImage(const QString& text, int moduleSize);

// The Steam Guard key as a QR code and an otpauth link, for an authenticator application that supports Steam. The
// code opens blurred and shows only on request, since anyone who sees it can take the key.
class SteamGuardQrDialog final : public QDialog {
	Q_OBJECT

public:
	SteamGuardQrDialog(const QString& accountTitle, QString steamGuardUri, QWidget* parent = nullptr);

private:
	void SetCodeShown(bool isShown);
	void ShowCopied(QPushButton& button, const QString& text);

	QString uri;
	QImage qrImage;
	QPixmap shownQrPixmap;
	QPixmap hiddenQrPixmap;
	QLabel* qrLabel = nullptr;
	QWidget* qrCover = nullptr;
	QPushButton* showCodeButton = nullptr;
	QPushButton* hideCodeButton = nullptr;
	QPushButton* copyImageButton = nullptr;
	QPushButton* copyLinkButton = nullptr;
	QTimer copiedFeedbackTimer;
};

} // namespace SteamForge
