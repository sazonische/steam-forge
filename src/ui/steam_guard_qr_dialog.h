#pragma once

namespace SteamForge {

// Black modules on white with the quiet zone the QR standard asks for, so phone cameras read it at once. The square
// has the given edge whatever the length of the text: modules take the largest whole size that fits, and what is
// left widens the white border evenly.
[[nodiscard]] QImage QrCodeImage(const QString& text, int edgeLength);

// The same Steam Guard key in the two forms of otpauth links that authenticator apps read.
struct SteamGuardLinks {
	QString steamEncoderLink;
	QString steamTypeLink;
};

// The Steam Guard key as a QR code and an otpauth link, for an authenticator application that supports Steam. The
// code opens blurred and shows only on request, since anyone who sees it can take the key. Tabs name the apps,
// since each reads only one form of the link.
class SteamGuardQrDialog final : public QDialog {
	Q_OBJECT

public:
	SteamGuardQrDialog(const QString& accountTitle, SteamGuardLinks steamGuardLinks, QWidget* parent = nullptr);

protected:
	void showEvent(QShowEvent* event) override;

private:
	void SelectLink(int linkIndex);
	void SetCodeShown(bool isShown);
	void ShowCopied(QPushButton& button, const QString& text);

	std::array<QString, 2> links;
	QString uri;
	bool isCodeShown = false;
	QButtonGroup appGroup;
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
