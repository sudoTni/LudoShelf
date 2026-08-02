#ifndef LUDOSHELF_DATABASE_DATABASEMANAGER_H
#define LUDOSHELF_DATABASE_DATABASEMANAGER_H

#include <QString>
#include <QSqlDatabase>
#include <QList>
#include <QUuid>
#include <QJsonObject>
#include <QDateTime>
#include <QSet>
#include <QHash>

#include "../domain/System.h"
#include "../domain/Game.h"
#include "../domain/GameFile.h"
#include "../domain/EmulatorProfile.h"
#include "../domain/PlaySession.h"
#include "../covers/CoverTypes.h"

namespace LudoShelf::Database {

struct ScanRoot {
    QUuid id{QUuid::createUuid()};
    QUuid systemId;
    QString path;
    bool recursive{true};
    bool followSymlinks{false};
    QStringList includeExtensions;
    QStringList excludeExtensions;
    QStringList excludePatterns;
    bool watchChanges{false};
};

struct GameMedia {
    QUuid id{QUuid::createUuid()};
    QUuid gameId;
    QString mediaType; // "box-front", "screenshot", "logo", "background"
    QString path;
    QString source;
    int width{0};
    int height{0};
    bool preferred{false};
};

struct DatSource {
    QUuid id{QUuid::createUuid()};
    QUuid systemId;
    QString name;
    QString version;
    QString author;
    QString category;
    QString filePath;
    QDateTime importedAt{QDateTime::currentDateTimeUtc()};
};

struct DatEntry {
    QUuid id{QUuid::createUuid()};
    QUuid datSourceId;
    QString gameName;
    QString romName;
    qint64 size{0};
    QString crc32;
    QString md5;
    QString sha1;
};

class DatabaseManager {
public:
    static DatabaseManager& instance();

    bool initialize(const QString& dbPath = QString());
    void close();

    QString databasePath() const;
    QSqlDatabase connection() const;
    bool checkIntegrity(QString& resultMessage);
    bool createBackup(const QString& backupPath = QString());

    // Systems CRUD
    QList<Domain::System> getSystems();
    Domain::System getSystem(const QUuid& id);
    bool saveSystem(const Domain::System& system);
    bool deleteSystem(const QUuid& id);

    // Scan Roots
    QList<ScanRoot> getScanRoots(const QUuid& systemId);
    bool saveScanRoot(const ScanRoot& root);
    bool deleteScanRoot(const QUuid& id);

    // Games CRUD
    QList<Domain::Game> getGamesForSystem(const QUuid& systemId);
    QList<Domain::Game> getAllGames();
    Domain::Game getGame(const QUuid& id);
    bool saveGame(const Domain::Game& game, const Domain::GameFile& primaryFile);
    bool saveGamesBatch(const QList<QPair<Domain::Game, Domain::GameFile>>& gamesList);
    // Fill only blank game-list fields with curated ROM metadata.  User and
    // scanner supplied values always take precedence.
    bool fillGameMetadataFields(const QUuid& gameId, const QDate& releaseDate,
                                const QString& developer, const QString& region,
                                const QString& publisher = {}, const QStringList& languages = {},
                                const QStringList& genres = {}, const QString& description = {});
    // Refresh scanner-owned fields without replacing user edits or enriched
    // ROM metadata already stored on the game record.
    bool updateScannedGame(const QUuid& gameId, const Domain::Game& scannedGame, const Domain::GameFile& primaryFile);
    bool deleteGame(const QUuid& id);

    // Game Files
    QList<Domain::GameFile> getFilesForGame(const QUuid& gameId);
    Domain::GameFile getPrimaryFileForGame(const QUuid& gameId);
    bool updateFileAvailability(const QUuid& fileId, bool available);
    // Reconcile a completed full-system scan.  Files not observed are marked
    // unavailable and a game is missing only when none of its files remain available.
    bool reconcileScannedFiles(const QUuid& systemId, const QSet<QString>& observedAbsolutePaths);
    bool updateFileHashes(const QUuid& fileId, const QString& crc32, const QString& md5, const QString& sha1,
                          const QUuid& datMatchId = {});

    // Game Media
    QList<GameMedia> getMediaForGame(const QUuid& gameId);
    bool saveGameMedia(const GameMedia& media);

    // Credential-free cover-art store.  The legacy game_media methods above are
    // retained for compatibility with pre-cover-pipeline libraries.
    bool saveCoverProvider(const Covers::CoverProvider& provider);
    Covers::CoverProvider getCoverProvider(const QString& providerId);
    bool saveMediaObject(const Covers::MediaObject& object);
    Covers::MediaObject getMediaObject(const QString& sha256);
    bool saveCoverAsset(const Covers::CoverAsset& asset);
    QList<Covers::CoverAsset> getCoverAssetsForGame(const QUuid& gameId);
    Covers::CoverAsset getPreferredCoverAsset(const QUuid& gameId);
    QHash<QUuid, QString> getPreferredCoverObjectHashes();
    bool setPreferredCoverAsset(const QUuid& gameId, const QUuid& assetId);
    bool saveCoverJob(const Covers::CoverJob& job);
    QList<Covers::CoverJob> getRunnableCoverJobs(const QDateTime& now, int limit = 20);

    // Emulators CRUD
    QList<Domain::EmulatorProfile> getEmulators();
    Domain::EmulatorProfile getEmulator(const QUuid& id);
    bool saveEmulator(const Domain::EmulatorProfile& emulator);
    bool deleteEmulator(const QUuid& id);
    bool setSystemDefaultEmulator(const QUuid& systemId, const QUuid& emulatorId);
    QUuid getSystemDefaultEmulator(const QUuid& systemId);

    // Play Sessions
    bool savePlaySession(const Domain::PlaySession& session);
    bool markGameLaunching(const QUuid& gameId);
    bool recordCompletedPlay(const Domain::PlaySession& session);
    // A detached child cannot be observed; keep an audit record without
    // treating it as a completed play or inventing a duration/exit status.
    bool recordDetachedLaunch(const Domain::PlaySession& session);
    bool restoreInterruptedGameStatuses();
    QList<Domain::PlaySession> getPlaySessionsForGame(const QUuid& gameId);

    // DAT Support
    bool saveDatSource(const DatSource& source, const QList<DatEntry>& entries);
    QList<DatSource> getDatSources(const QUuid& systemId);
    QList<DatEntry> getDatEntriesForSource(const QUuid& sourceId);
    bool matchDatEntry(const QUuid& systemId, const QString& crc32, const QString& sha1, DatEntry& matchedEntry);

private:
    DatabaseManager() = default;
    ~DatabaseManager();
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    bool createTables();
    bool migrateSchema();
    bool deduplicateCoverAssets();
    QString m_connectionName{"ludoshelf_primary"};
    QString m_dbFilePath;
};

} // namespace LudoShelf::Database

#endif // LUDOSHELF_DATABASE_DATABASEMANAGER_H
