#include <QtTest/QtTest>
#include <algorithm>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "../src/app/AppPaths.h"
#include "../src/covers/CoverTitleNormalizer.h"
#include "../src/covers/LocalCoverDiscovery.h"
#include "../src/covers/CoverScorer.h"
#include "../src/covers/LibretroCoverProvider.h"
#include "../src/covers/LibretroThumbnailCatalog.h"
#include "../src/covers/RetroArchCoverDiscovery.h"
#include "../src/database/DatabaseManager.h"
#include "../src/ui/SystemPresets.h"

class TestCoverFoundation : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().initialize(":memory:"));
    }

    void normalizesOnlyKnownTags() {
        const auto lookup = LudoShelf::Covers::CoverTitleNormalizer::normalize("Doom (2016) (USA) [!]");
        QCOMPARE(lookup.canonicalTitle, QString("Doom (2016)"));
        QCOMPARE(lookup.shortTitle, QString("Doom"));
        QCOMPARE(lookup.extractedRegions, QStringList{"USA"});
    }

    void normalizesGoodGenTitlesForLibretro() {
        const auto lookup = LudoShelf::Covers::CoverTitleNormalizer::normalize("Zombies Ate My Neighbors (U) [c][!]");
        QCOMPARE(lookup.canonicalTitle, QString("Zombies Ate My Neighbors"));
        QCOMPARE(lookup.extractedRegions, QStringList{"USA"});
        const QStringList candidates = LudoShelf::Covers::CoverTitleNormalizer::libretroTitleCandidates({lookup.original});
        QVERIFY(candidates.contains("Zombies Ate My Neighbors (USA)"));
    }

    void blocksSidecarTraversal() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QVERIFY(LudoShelf::Covers::LocalCoverDiscovery::resolveSidecarPath(directory.path(), "../../etc/passwd").isEmpty());
        QVERIFY(LudoShelf::Covers::LocalCoverDiscovery::resolveSidecarPath(directory.path(), "file:///etc/passwd").isEmpty());
    }

    void scoresPreferredAuthenticCoverHigher() {
        LudoShelf::Covers::CoverCandidate box;
        box.sourcePriority = 700; box.kind = LudoShelf::Covers::CoverKind::BoxFront;
        box.region = "US"; box.matchConfidence = 1; box.declaredWidth = 600; box.declaredHeight = 900;
        LudoShelf::Covers::CoverCandidate capsule = box;
        capsule.kind = LudoShelf::Covers::CoverKind::StoreVerticalCapsule;
        QVERIFY(LudoShelf::Covers::CoverScorer::score(box, "US", false, true) >
                 LudoShelf::Covers::CoverScorer::score(capsule, "US", false, true));
    }

    void mapsDreamcastGameCubeAndPsxToLibretroCollections() {
        LudoShelf::Domain::Game game;
        game.title = "Example Game";
        for (const auto& pair : std::initializer_list<std::pair<QString, QString>>{
                 {"dc", "Sega_-_Dreamcast"}, {"gamecube", "Nintendo_-_GameCube"},
                 {"nintendogamecb", "Nintendo_-_GameCube"},
                 {"psx", "Sony_-_PlayStation"}, {"Sega Genesis", "Sega_-_Mega_Drive_-_Genesis"}}) {
            LudoShelf::Domain::System system;
            system.name = "Custom Name";
            system.shortName = pair.first;
            const auto candidates = LudoShelf::Covers::LibretroCoverProvider::candidatesFor(game, system);
            QVERIFY(!candidates.isEmpty());
            QVERIFY(candidates.first().downloadUrl.toString(QUrl::FullyEncoded).contains(pair.second));
        }
    }

    void exposesTheCompleteHardwarePresetCatalogWithCanonicalCollections() {
        const auto& presets = LudoShelf::UI::systemPresets();
        QCOMPARE(presets.size(), 100);
        QCOMPARE(LudoShelf::UI::matchingSystemPreset("Nintendo - Nintendo Entertainment System", "nintendo"), 56);
        QCOMPARE(LudoShelf::UI::matchingSystemPreset("Nintendo - GameCube", "gamecube"), 50);
        QCOMPARE(LudoShelf::UI::matchingSystemPreset("Nintendo GameCube", "nintendogamecb"), -1);
        LudoShelf::Domain::Game game;
        game.title = "Example Game";
        LudoShelf::Domain::System system;
        system.name = "Atari - 2600";
        system.shortName = "atari2600";
        const auto candidates = LudoShelf::Covers::LibretroCoverProvider::candidatesFor(game, system);
        QVERIFY(!candidates.isEmpty());
        QVERIFY(candidates.first().downloadUrl.toString(QUrl::FullyEncoded).contains("Atari_-_2600"));
    }

    void mapsSuperNintendoAliasesToLibretroCollection() {
        LudoShelf::Domain::Game game;
        game.title = "ActRaiser";
        LudoShelf::Domain::System system;
        system.name = "Super Nintendo";
        system.shortName = "supernintendo";

        const auto candidates = LudoShelf::Covers::LibretroCoverProvider::candidatesFor(game, system);
        QVERIFY(!candidates.isEmpty());
        QVERIFY(candidates.first().downloadUrl.toString(QUrl::FullyEncoded)
                    .contains("Nintendo_-_Super_Nintendo_Entertainment_System"));
        QCOMPARE(LudoShelf::Covers::LibretroCoverProvider::collectionForMetadataPlatform("Super Nintendo"),
                 QString("Nintendo - Super Nintendo Entertainment System"));
    }

    void mapsNintendoAliasToNesCollection() {
        LudoShelf::Domain::Game game;
        game.title = "Metroid";
        LudoShelf::Domain::System system;
        system.name = "Nintendo";
        system.shortName = "nintendo";
        const auto candidates = LudoShelf::Covers::LibretroCoverProvider::candidatesFor(game, system);
        QVERIFY(!candidates.isEmpty());
        QVERIFY(candidates.first().downloadUrl.toString(QUrl::FullyEncoded)
                    .contains("Nintendo_-_Nintendo_Entertainment_System"));
    }

    void mapsNintendo64AliasToCanonicalCollection() {
        LudoShelf::Domain::Game game;
        game.title = "Cruis'n World";
        LudoShelf::Domain::System system;
        system.name = "Nintendo 64";
        system.shortName = "nintendo64";
        const auto candidates = LudoShelf::Covers::LibretroCoverProvider::candidatesFor(game, system);
        QVERIFY(!candidates.isEmpty());
        QVERIFY(candidates.first().downloadUrl.toString(QUrl::FullyEncoded)
                    .contains("Nintendo_-_Nintendo_64"));
        QCOMPARE(LudoShelf::Covers::LibretroCoverProvider::collectionForMetadataPlatform("nintendo64"),
                 QString("Nintendo - Nintendo 64"));
    }

    void includes32xCollectionAndNormalizedTitleCandidates() {
        LudoShelf::Domain::Game game;
        game.title = "Afterburner 32X (5) [!]";
        LudoShelf::Domain::System system;
        system.name = "Sega Genesis";
        system.shortName = "segagenesis";
        const auto candidates = LudoShelf::Covers::LibretroCoverProvider::candidatesFor(
            game, system, {"Afterburner 32X (5) [!]"});
        QVERIFY(std::any_of(candidates.cbegin(), candidates.cend(), [](const auto& candidate) {
            return candidate.downloadUrl.toString(QUrl::FullyEncoded).contains("Sega_-_32X") &&
                   candidate.providerTitle == "Afterburner 32X";
        }));
    }

    void selectsOfficialCatalogAssetForCanonicalAndAliasTitles() {
        const QStringList assets{
            "Named_Boxarts/Road Rash II (USA, Europe) (RR205).png",
            "Named_Boxarts/Vectorman 2 (USA).png",
            "Named_Boxarts/Wonder Boy in Monster World (USA, Europe).png",
            "Named_Boxarts/Unrelated Game (USA).png"
        };
        const auto vectorman = LudoShelf::Covers::LibretroThumbnailCatalog::selectAssetPaths(
            assets, {"Vectorman 2 (USA)"});
        QCOMPARE(vectorman, QStringList{"Named_Boxarts/Vectorman 2 (USA).png"});

        const auto aliases = LudoShelf::Covers::LibretroThumbnailCatalog::selectAssetPaths(
            assets, {"Road Rash II (USA, Europe)", "Wonder Boy in Monster World (EU-US)"});
        QVERIFY(aliases.contains("Named_Boxarts/Road Rash II (USA, Europe) (RR205).png"));
        QVERIFY(aliases.contains("Named_Boxarts/Wonder Boy in Monster World (USA, Europe).png"));
    }

    void matchesRomanNumeralsPunctuationAndUnambiguousSubtitles() {
        const QStringList assets{
            "Named_Boxarts/Double Dragon II - The Revenge (USA).png",
            "Named_Boxarts/Dragon Warrior II (USA).png",
            "Named_Boxarts/IronSword - Wizards _ Warriors II (USA).png",
            "Named_Boxarts/M.C. Kids (USA).png",
            "Named_Boxarts/Teenage Mutant Ninja Turtles III - The Manhattan Project (USA).png"
        };
        const auto matches = LudoShelf::Covers::LibretroThumbnailCatalog::selectAssetPaths(assets, {
            "Double Dragon 2 - The Revenge", "Dragon Warrior 2", "Ironsword - Wizards & Warriors 2",
            "MC Kids", "Teenage Mutant Ninja Turtles 3"
        });
        QCOMPARE(matches.size(), 3);
        QVERIFY(matches.contains("Named_Boxarts/Double Dragon II - The Revenge (USA).png"));
        QVERIFY(matches.contains("Named_Boxarts/Dragon Warrior II (USA).png"));
        QVERIFY(matches.contains("Named_Boxarts/IronSword - Wizards _ Warriors II (USA).png"));

        QCOMPARE(LudoShelf::Covers::LibretroThumbnailCatalog::selectAssetPaths(assets, {"MC Kids"}),
                 QStringList{"Named_Boxarts/M.C. Kids (USA).png"});
        QCOMPARE(LudoShelf::Covers::LibretroThumbnailCatalog::selectAssetPaths(assets, {"Teenage Mutant Ninja Turtles 3"}),
                 QStringList{"Named_Boxarts/Teenage Mutant Ninja Turtles III - The Manhattan Project (USA).png"});
    }

    void resolvesCachedCatalogAsynchronously() {
        const QString collection = "Test - Async Catalog";
        const QString cacheDirectory = QDir(LudoShelf::App::AppPaths::cacheRoot()).filePath("libretro-thumbnail-catalog");
        QVERIFY(QDir().mkpath(cacheDirectory));
        const QString cachePath = QDir(cacheDirectory).filePath("Test_-_Async_Catalog.json");
        QFile cache(cachePath);
        QVERIFY(cache.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray catalog = QStringLiteral("{\"fetchedAt\":\"%1\",\"etag\":\"test\",\"assets\":[\"Named_Boxarts/Cruis'n World (USA).png\"]}")
            .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate)).toUtf8();
        QCOMPARE(cache.write(catalog), static_cast<qint64>(catalog.size()));
        cache.close();

        LudoShelf::Covers::LibretroThumbnailCatalog thumbnailCatalog;
        constexpr int RequestCount = 128;
        int completed = 0;
        QStringList firstResolved;
        for (int request = 0; request < RequestCount; ++request) {
            thumbnailCatalog.resolve(collection, {"Cruis'n World (USA)"}, [&completed, &firstResolved](const QStringList& paths) {
                if (firstResolved.isEmpty()) firstResolved = paths;
                ++completed;
            });
        }
        QCOMPARE(completed, 0);
        QTRY_COMPARE_WITH_TIMEOUT(completed, RequestCount, 3000);
        QCOMPARE(firstResolved, QStringList{"Named_Boxarts/Cruis'n World (USA).png"});
        QVERIFY(QFile::remove(cachePath));
    }

    void createsRawOnlyCatalogCandidate() {
        LudoShelf::Domain::Game game;
        game.title = "Altered Beast";
        LudoShelf::Domain::System system;
        system.name = "Sega Genesis";
        system.shortName = "genesis";
        const auto candidates = LudoShelf::Covers::LibretroCoverProvider::candidatesForCatalogAssets(
            game, system, "Sega - Mega Drive - Genesis", {"Named_Boxarts/Altered Beast (USA, Europe) (Rev 2).png"});
        QCOMPARE(candidates.size(), 1);
        QCOMPARE(candidates.first().matchMethod, QString("thumbnail_catalog_match"));
        QVERIFY(candidates.first().downloadUrl.host() == "raw.githubusercontent.com");
        QCOMPARE(LudoShelf::Covers::LibretroCoverProvider::collectionForMetadataPlatform("Sega - 32X"),
                 QString("Sega - 32X"));
    }

    void reusesExactRetroArchPlaylistCover() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString configRoot = temporary.filePath("config");
        const QString thumbnails = temporary.filePath("thumbnails");
        QVERIFY(QDir().mkpath(configRoot + "/playlists"));
        QVERIFY(QDir().mkpath(thumbnails + "/Sony - PlayStation/Named_Boxarts"));

        const QString romPath = temporary.filePath("Example Game.bin");
        QFile rom(romPath);
        QVERIFY(rom.open(QIODevice::WriteOnly));
        rom.write("disc image placeholder");
        rom.close();

        const QJsonObject item{{"path", romPath}, {"label", "Example Game (USA)"}, {"db_name", "Sony - PlayStation.lpl"}};
        const QJsonDocument playlist(QJsonObject{{"items", QJsonArray{item}}});
        QFile playlistFile(configRoot + "/playlists/Sony - PlayStation.lpl");
        QVERIFY(playlistFile.open(QIODevice::WriteOnly));
        playlistFile.write(playlist.toJson());
        playlistFile.close();

        QImage image(200, 300, QImage::Format_RGB32);
        image.fill(Qt::blue);
        QVERIFY(image.save(thumbnails + "/Sony - PlayStation/Named_Boxarts/Example Game.png"));

        LudoShelf::Domain::Game game;
        game.title = "Example Game";
        LudoShelf::Domain::GameFile file;
        file.path = romPath;
        LudoShelf::Domain::System system;
        system.shortName = "psx";
        const auto candidates = LudoShelf::Covers::RetroArchCoverDiscovery::findLocalCandidates(
            game, file, system, nullptr, {configRoot}, {thumbnails});
        QVERIFY(!candidates.isEmpty());
        QCOMPARE(candidates.first().providerId, QString("retroarch-cache"));
        QCOMPARE(candidates.first().matchMethod, QString("retroarch_rom_filename"));
    }

    void enforcesOnePreferredDisplayCover() {
        LudoShelf::Domain::System system;
        system.name = "Test System";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveSystem(system));
        LudoShelf::Domain::Game game;
        game.systemId = system.id;
        game.title = "Test Game";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveGame(game, {}));

        LudoShelf::Covers::CoverProvider provider;
        provider.id = "test-provider";
        provider.displayName = "Test Provider";
        provider.adapterVersion = "1";
        provider.stability = "test";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveCoverProvider(provider));

        LudoShelf::Covers::CoverAsset first;
        first.gameId = game.id; first.providerId = provider.id; first.kind = LudoShelf::Covers::CoverKind::BoxFront;
        first.matchMethod = "test"; first.rightsStatus = "test"; first.preferred = true;
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveCoverAsset(first));
        LudoShelf::Covers::CoverAsset second = first;
        second.id = QUuid::createUuid(); second.kind = LudoShelf::Covers::CoverKind::LibraryVerticalArt;
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveCoverAsset(second));
        const auto assets = LudoShelf::Database::DatabaseManager::instance().getCoverAssetsForGame(game.id);
        QCOMPARE(std::count_if(assets.cbegin(), assets.cend(), [](const auto& a) { return a.preferred; }), 1);
    }

    void makesRepeatedProviderAssetStorageIdempotent() {
        LudoShelf::Domain::System system;
        system.name = "Deduplication System";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveSystem(system));
        LudoShelf::Domain::Game game;
        game.systemId = system.id;
        game.title = "Deduplication Game";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveGame(game, {}));

        LudoShelf::Covers::CoverProvider provider;
        provider.id = "dedupe-provider";
        provider.displayName = "Dedupe";
        provider.adapterVersion = "1";
        provider.stability = "test";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveCoverProvider(provider));
        LudoShelf::Covers::CoverAsset asset;
        asset.gameId = game.id; asset.providerId = provider.id; asset.kind = LudoShelf::Covers::CoverKind::BoxFront;
        asset.matchMethod = "test"; asset.rightsStatus = "test"; asset.preferred = true;
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveCoverAsset(asset));
        asset.id = QUuid::createUuid();
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveCoverAsset(asset));
        QCOMPARE(LudoShelf::Database::DatabaseManager::instance().getCoverAssetsForGame(game.id).size(), 1);
    }

    void refreshesScannerFieldsWithoutDiscardingEnrichedMetadata() {
        LudoShelf::Domain::System system;
        system.name = "Scanner Refresh System";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveSystem(system));
        LudoShelf::Domain::Game game;
        game.systemId = system.id; game.title = "Old Title"; game.description = "Preserved enrichment";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveGame(game, {}));
        LudoShelf::Domain::Game scanned;
        scanned.title = "New Title"; scanned.sortTitle = "New Title"; scanned.region = "USA";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().updateScannedGame(game.id, scanned, {}));
        const auto refreshed = LudoShelf::Database::DatabaseManager::instance().getGame(game.id);
        QCOMPARE(refreshed.title, QString("New Title"));
        QCOMPARE(refreshed.region, QString("USA"));
        QCOMPARE(refreshed.description, QString("Preserved enrichment"));
    }

    void persistsExtendedGameMetadata() {
        LudoShelf::Domain::System system;
        system.name = "Metadata System";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveSystem(system));

        LudoShelf::Domain::Game game;
        game.systemId = system.id;
        game.title = "Metadata Game";
        game.description = "A complete metadata record.";
        game.developer = "Developer";
        game.publisher = "Publisher";
        game.languages = {"English", "Japanese"};
        game.genres = {"Action", "Platformer"};
        game.series = "Metadata Series";
        game.playersMin = 1;
        game.playersMax = 4;
        game.notes = "Personal note";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveGame(game, {}));

        const auto loaded = LudoShelf::Database::DatabaseManager::instance().getGame(game.id);
        QCOMPARE(loaded.description, game.description);
        QCOMPARE(loaded.languages, game.languages);
        QCOMPARE(loaded.genres, game.genres);
        QCOMPARE(loaded.series, game.series);
        QCOMPARE(loaded.playersMin, 1);
        QCOMPARE(loaded.playersMax, 4);
        QCOMPARE(loaded.notes, game.notes);
    }

    void recordsCompletedPlayAsPlayed() {
        LudoShelf::Domain::System system;
        system.name = "Play System";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveSystem(system));
        LudoShelf::Domain::Game game;
        game.systemId = system.id;
        game.title = "Played Game";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveGame(game, {}));

        LudoShelf::Domain::PlaySession session;
        session.gameId = game.id;
        session.startedAt = QDateTime::currentDateTimeUtc().addSecs(-42);
        session.endedAt = QDateTime::currentDateTimeUtc();
        session.durationSeconds = 42;
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().recordCompletedPlay(session));

        const auto loaded = LudoShelf::Database::DatabaseManager::instance().getGame(game.id);
        QCOMPARE(loaded.playCount, 1);
        QCOMPARE(loaded.totalPlaySeconds, 42);
        QCOMPARE(loaded.status, QString("Played"));
        QVERIFY(loaded.lastPlayed.isValid());
        QCOMPARE(LudoShelf::Database::DatabaseManager::instance().getPlaySessionsForGame(game.id).size(), 1);
    }

    void restoresTemporaryPlayingStatusAfterLaunchOrRestart() {
        LudoShelf::Domain::System system;
        system.name = "Temporary Status System";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveSystem(system));
        LudoShelf::Domain::Game game;
        game.systemId = system.id;
        game.title = "Temporary Status Game";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveGame(game, {}));

        QVERIFY(LudoShelf::Database::DatabaseManager::instance().markGameLaunching(game.id));
        QCOMPARE(LudoShelf::Database::DatabaseManager::instance().getGame(game.id).status, QString("Playing"));

        LudoShelf::Domain::PlaySession session;
        session.gameId = game.id;
        session.endedAt = QDateTime::currentDateTimeUtc();
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().recordCompletedPlay(session));
        QCOMPARE(LudoShelf::Database::DatabaseManager::instance().getGame(game.id).status, QString("Played"));

        QVERIFY(LudoShelf::Database::DatabaseManager::instance().markGameLaunching(game.id));
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().restoreInterruptedGameStatuses());
        const auto restored = LudoShelf::Database::DatabaseManager::instance().getGame(game.id);
        QCOMPARE(restored.status, QString("Played"));
        QCOMPARE(restored.playCount, 2);
    }

    void migratesLegacyMediaObjectsForCoverStorage() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString databasePath = temporary.filePath("legacy-library.db");

        LudoShelf::Database::DatabaseManager::instance().close();
        {
            QSqlDatabase legacy = QSqlDatabase::addDatabase("QSQLITE", "legacy_cover_schema");
            legacy.setDatabaseName(databasePath);
            QVERIFY(legacy.open());
            QSqlQuery query(legacy);
            QVERIFY(query.exec(R"(
                CREATE TABLE media_objects (
                    sha256 TEXT PRIMARY KEY,
                    relative_path TEXT NOT NULL,
                    mime_type TEXT,
                    byte_size INTEGER DEFAULT 0,
                    width INTEGER DEFAULT 0,
                    height INTEGER DEFAULT 0,
                    created_at TEXT,
                    reference_count INTEGER DEFAULT 1
                )
            )"));
            QVERIFY(query.exec("INSERT INTO media_objects VALUES "
                               "('aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa', "
                               "'/tmp/legacy-cover.png', 'image/png', 1, 200, 300, '', 1)"));
            legacy.close();
        }
        QSqlDatabase::removeDatabase("legacy_cover_schema");

        QVERIFY(LudoShelf::Database::DatabaseManager::instance().initialize(databasePath));
        const auto migrated = LudoShelf::Database::DatabaseManager::instance().getMediaObject(
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
        QCOMPARE(migrated.extension, QString("png"));
        QCOMPARE(migrated.validationState, QString("validated"));

        LudoShelf::Covers::MediaObject current;
        current.sha256 = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        current.relativePath = "media/objects/bb/bb/current.png";
        current.mimeType = "image/png";
        current.extension = "png";
        current.byteSize = 1;
        current.width = 200;
        current.height = 300;
        current.validationState = "validated";
        QVERIFY(LudoShelf::Database::DatabaseManager::instance().saveMediaObject(current));
        LudoShelf::Database::DatabaseManager::instance().close();
    }
};

QTEST_MAIN(TestCoverFoundation)
#include "test_cover_foundation.moc"
