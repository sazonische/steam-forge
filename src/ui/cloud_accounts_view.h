#pragma once

#include "domain/authenticator_record.h"

namespace SteamForge {

class AccountLibrary;
class CloudSync;
class GoogleConnection;
struct CloudOnlyRecord;

// The accounts of this computer, each with a tick for Google, and the accounts left only in Google. A tick only
// asks: the window that owns the view confirms a removal and starts the change.
class CloudAccountsView final : public QWidget {
	Q_OBJECT

public:
	using AccountTitleProvider = std::function<QString(const AuthenticatorRecord&)>;

	CloudAccountsView(const CloudSync& sync, const GoogleConnection& connection, const AccountLibrary& library, AccountTitleProvider titleProvider, QWidget* parent = nullptr);

Q_SIGNALS:
	void AddRequested(const QUuid& recordId);
	void RemoveRequested(const QUuid& recordId);
	void ConflictRequested(const QUuid& recordId);
	void ReturnRequested(const QString& cloudRecordId);
	void SyncRequested();

protected:
	void changeEvent(QEvent* event) override;

private:
	void RetranslateUi();
	void Rebuild();
	[[nodiscard]] QWidget* CreateRecordRow(const AuthenticatorRecord& record);
	[[nodiscard]] QWidget* CreateCloudOnlyRow(const CloudOnlyRecord& record);
	[[nodiscard]] QString SyncText() const;

	const CloudSync& cloudSync;
	const GoogleConnection& googleConnection;
	const AccountLibrary& accountLibrary;
	AccountTitleProvider accountTitleProvider;
	// Changes come in bursts and from inside the handlers of the rows: the rows are rebuilt once, afterwards.
	QTimer rebuildTimer;

	QLabel* accountsSectionLabel = nullptr;
	QLabel* hintLabel = nullptr;
	QLabel* syncLabel = nullptr;
	QPushButton* syncButton = nullptr;
	QWidget* rowsWidget = nullptr;
	QVBoxLayout* rowsLayout = nullptr;
};

} // namespace SteamForge
