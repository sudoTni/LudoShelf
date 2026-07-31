#include <QtTest>
#include "../src/app/AppPaths.h"
#include "../src/media/MediaStorageManager.h"
#include "../src/database/DatabaseManager.h"
#include "../src/models/GameTableModel.h"

using namespace LudoShelf;

class TestMediaStorage : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        Database::DatabaseManager::instance().initialize(":memory:");
    }

    void testContentAddressedStorage() {
        QUuid gameId = QUuid::createUuid();

        QImage img(100, 100, QImage::Format_RGB32);
        img.fill(Qt::red);
        QByteArray data;
        QBuffer buf(&data);
        buf.open(QIODevice::WriteOnly);
        img.save(&buf, "PNG");

        QString sha256 = Media::MediaStorageManager::instance().storeOriginalImage(
            gameId, data, "box-front", "image/png", "local"
        );

        QVERIFY(!sha256.isEmpty());
        QCOMPARE(sha256.length(), 64);
        QVERIFY(Media::MediaStorageManager::instance().getObjectAbsolutePath(sha256)
                    .startsWith(App::AppPaths::dataRoot() + "/"));

        // Verify deduplication
        QString duplicateSha256 = Media::MediaStorageManager::instance().storeOriginalImage(
            gameId, data, "box-front", "image/png", "local"
        );

        QCOMPARE(sha256, duplicateSha256);

        // Verify thumbnail scaling
        QImage thumb = Media::MediaStorageManager::instance().loadThumbnail(sha256, 50, 50);
        QVERIFY(!thumb.isNull());
        QCOMPARE(thumb.width(), 50);
        QCOMPARE(thumb.height(), 50);
    }

    void fillsOnlyBlankGameListMetadataFields() {
        Domain::System system;
        system.name = "Metadata Test System";
        QVERIFY(Database::DatabaseManager::instance().saveSystem(system));
        Domain::Game game;
        game.systemId = system.id;
        game.title = "Metadata fixture";
        Domain::GameFile file;
        file.gameId = game.id;
        file.path = "/virtual/metadata-fixture.rom";
        QVERIFY(Database::DatabaseManager::instance().saveGame(game, file));
        QVERIFY(Database::DatabaseManager::instance().fillGameMetadataFields(
            game.id, QDate(1994, 1, 1), "Fixture Studio", "USA"));
        auto stored = Database::DatabaseManager::instance().getGame(game.id);
        QCOMPARE(stored.releaseDate, QDate(1994, 1, 1));
        QCOMPARE(stored.developer, QString("Fixture Studio"));
        QCOMPARE(stored.region, QString("USA"));

        stored.developer = "User Studio";
        QVERIFY(Database::DatabaseManager::instance().saveGame(stored, file));
        QVERIFY(Database::DatabaseManager::instance().fillGameMetadataFields(
            game.id, QDate(1995, 1, 1), "Other Studio", "Japan"));
        stored = Database::DatabaseManager::instance().getGame(game.id);
        QCOMPARE(stored.releaseDate, QDate(1994, 1, 1));
        QCOMPARE(stored.developer, QString("User Studio"));
        QCOMPARE(stored.region, QString("USA"));
    }

    void coverFallbackIsReadOnlyForTheModel() {
        Domain::Game game;
        game.title = "Uncovered Game";

        Models::GameTableModel model;
        model.setGames({game});
        const QVariant artwork = model.data(
            model.index(0, Models::GameTableModel::ColumnTitle), Models::GameTableModel::CoverPixmapRole);

        QVERIFY(artwork.canConvert<QPixmap>());
        QVERIFY(!artwork.value<QPixmap>().isNull());
        QVERIFY(Database::DatabaseManager::instance().getMediaForGame(game.id).isEmpty());
        QVERIFY(Database::DatabaseManager::instance().getCoverAssetsForGame(game.id).isEmpty());
    }
};

QTEST_MAIN(TestMediaStorage)
#include "test_media_storage.moc"
