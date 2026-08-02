#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

#include "../src/app/LibraryBackupService.h"
#include "../src/database/DatabaseManager.h"

using namespace LudoShelf;

class TestLibraryBackup : public QObject {
    Q_OBJECT
private slots:
    void replaceImportRestoresSnapshotAndRejectsMalformedInputWithoutMutation() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString database = temporary.filePath("library.db");
        QVERIFY(Database::DatabaseManager::instance().initialize(database));

        Domain::System original;
        original.name = "Original system";
        QVERIFY(Database::DatabaseManager::instance().saveSystem(original));
        const QString snapshot = temporary.filePath("snapshot.json");
        QVERIFY(App::LibraryBackupService::exportLibraryToJson(snapshot));

        Domain::System extra;
        extra.name = "Extra system";
        QVERIFY(Database::DatabaseManager::instance().saveSystem(extra));
        QCOMPARE(Database::DatabaseManager::instance().getSystems().size(), 2);
        QVERIFY(App::LibraryBackupService::importLibraryFromJson(snapshot));
        const auto restored = Database::DatabaseManager::instance().getSystems();
        QCOMPARE(restored.size(), 1);
        QCOMPARE(restored.first().name, original.name);

        const QString malformed = temporary.filePath("malformed.json");
        QFile invalid(malformed);
        QVERIFY(invalid.open(QIODevice::WriteOnly));
        invalid.write("{\"format\":\"ludoshelf-library-snapshot\",\"version\":2,\"tables\":{}}");
        invalid.close();
        QVERIFY(!App::LibraryBackupService::importLibraryFromJson(malformed));
        QCOMPARE(Database::DatabaseManager::instance().getSystems().size(), 1);
        Database::DatabaseManager::instance().close();
    }
};

QTEST_MAIN(TestLibraryBackup)
#include "test_library_backup.moc"
