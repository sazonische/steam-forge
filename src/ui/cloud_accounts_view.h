#pragma once

#include "domain/authenticator_record.h"

namespace SteamForge {

class AccountLibrary;
class AccountProfiles;
class CloudSync;
class GoogleConnection;
class ScrollingRowList;
struct CloudOnlyRecord;

// The accounts of this computer, each with a tick for Google, and the accounts left only in Google. A tick only
// asks: the window that owns the view confirms a removal and starts the change.
class CloudAccountsView final : public QWidget {
	Q_OBJECT

public:
	using AccountTitleProvider = std::function<QString(const AuthenticatorRecord&)>;

	// Without profiles the rows show no avatars.
	CloudAccountsView(const CloudSync& sync, const GoogleConnection& connection, const AccountLibrary& library, AccountTitleProvider titleProvider, AccountProfiles* profiles, QWidget* parent = nullptr);

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
	AccountProfiles* accountProfiles = nullptr;
	// Changes come in bursts and from inside the handlers of the rows: the rows are rebuilt once, afterwards.
	QTimer rebuildTimer;

	QLabel* accountsSectionLabel = nullptr;
	QLabel* hintLabel = nullptr;
	QLabel* syncLabel = nullptr;
	QPushButton* syncButton = nullptr;
	ScrollingRowList* rowList = nullptr;
};

} // namespace SteamForge
