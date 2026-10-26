#include "ui/import_dialog.h"

namespace {
void CreateFile(const QDir& directory, const QString& fileName) {
	QFile file(directory.filePath(fileName));
	QVERIFY(file.open(QIODevice::WriteOnly));
}
} // namespace

class DroppedFilesTest final : public QObject {
	Q_OBJECT

private Q_SLOTS:
	void TakesTheAuthenticatorFilesOfADroppedFolder() {
		const QTemporaryDir temporaryDirectory;
		const QDir maFilesFolder(temporaryDirectory.path());
		CreateFile(maFilesFolder, QStringLiteral("76561198000000001.maFile"));
		CreateFile(maFilesFolder, QStringLiteral("twofactor_76561198000000002.json"));
		CreateFile(maFilesFolder, QStringLiteral("manifest.json"));
		CreateFile(maFilesFolder, QStringLiteral("notes.txt"));
		QMimeData mimeData;
		mimeData.setUrls({QUrl::fromLocalFile(maFilesFolder.absolutePath())});

		const QStringList filePaths = SteamForge::DroppedFilePaths(mimeData);
		QCOMPARE(filePaths.size(), 2);
		QVERIFY(filePaths.contains(maFilesFolder.absoluteFilePath(QStringLiteral("76561198000000001.maFile"))));
		QVERIFY(filePaths.contains(maFilesFolder.absoluteFilePath(QStringLiteral("twofactor_76561198000000002.json"))));
	}

	void KeepsDroppedFilesWhateverTheirName() {
		const QTemporaryDir temporaryDirectory;
		const QDir folder(temporaryDirectory.path());
		CreateFile(folder, QStringLiteral("winauth-export.txt"));
		QMimeData mimeData;
		mimeData.setUrls({QUrl::fromLocalFile(folder.absoluteFilePath(QStringLiteral("winauth-export.txt"))), QUrl(QStringLiteral("https://example.invalid/file.maFile"))});

		const QStringList filePaths = SteamForge::DroppedFilePaths(mimeData);
		QCOMPARE(filePaths, QStringList{folder.absoluteFilePath(QStringLiteral("winauth-export.txt"))});
	}
};

QTEST_GUILESS_MAIN(DroppedFilesTest)

#include "dropped_files_test.moc"
