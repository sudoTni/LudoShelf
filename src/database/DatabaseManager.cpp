#include "DatabaseManager.h"
#include "../app/AppPaths.h"

#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QDateTime>
#include <QSet>
#include <QThread>
#include <QCoreApplication>

namespace LudoShelf::Database {
namespace {

QStringList decodeStringList(const QVariant& value) {
    const QString encoded = value.toString();
    const QJsonDocument document = QJsonDocument::fromJson(encoded.toUtf8());
    if (document.isArray()) return document.toVariant().toStringList();
    return encoded.split(',', Qt::SkipEmptyParts);
}

QString encodeStringList(const QStringList& values) {
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(values)).toJson(QJsonDocument::Compact));
}

} // namespace

DatabaseManager& DatabaseManager::instance() {
    static DatabaseManager mgr;
    return mgr;
}

DatabaseManager::~DatabaseManager() {
    close();
}

QString DatabaseManager::databasePath() const {
    return m_dbFilePath;
}

QSqlDatabase DatabaseManager::connection() const {
    if (m_dbFilePath.isEmpty() || !QCoreApplication::instance()) {
        return QSqlDatabase::database(m_connectionName);
    }
    if (QThread::currentThread() == QCoreApplication::instance()->thread()) {
        return QSqlDatabase::database(m_connectionName);
    }
    const QString threadConnName = QString("%1_thread_%2")
        .arg(m_connectionName)
        .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    if (QSqlDatabase::contains(threadConnName)) {
        QSqlDatabase db = QSqlDatabase::database(threadConnName);
        if (db.isOpen()) return db;
    }
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", threadConnName);
    db.setDatabaseName(m_dbFilePath);
    if (db.open()) {
        QSqlQuery q(db);
        q.exec("PRAGMA journal_mode = WAL;");
        q.exec("PRAGMA foreign_keys = ON;");
    }
    return db;
}

bool DatabaseManager::initialize(const QString& dbPath) {
    m_dbFilePath = dbPath;
    if (m_dbFilePath.isEmpty()) {
        if (!App::AppPaths::migrateLegacyDataIfNeeded()) return false;
        m_dbFilePath = App::AppPaths::databasePath();
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", m_connectionName);
    db.setDatabaseName(m_dbFilePath);

    if (!db.open()) {
        qCritical() << "Failed to open database:" << db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    if (!q.exec("PRAGMA journal_mode = WAL;") || !q.exec("PRAGMA foreign_keys = ON;")) {
        qCritical() << "Failed to configure database connection:" << q.lastError().text();
        return false;
    }

    return createTables() && migrateSchema() && deduplicateCoverAssets() && restoreInterruptedGameStatuses();
}

void DatabaseManager::close() {
    if (QSqlDatabase::contains(m_connectionName)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connectionName);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connectionName);
    }
}


bool DatabaseManager::checkIntegrity(QString& resultMessage) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("PRAGMA quick_check;", db);
    if (q.next()) {
        resultMessage = q.value(0).toString();
        return resultMessage.toLower() == "ok";
    }
    resultMessage = "Failed to run integrity check.";
    return false;
}

bool DatabaseManager::createBackup(const QString& backupPath) {
    QString target = backupPath;
    if (target.isEmpty()) {
        const QString backupDir = App::AppPaths::backupsRoot();
        target = QString("%1/ludoshelf_backup_%2.db")
            .arg(backupDir)
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
    }

    const QFileInfo targetInfo(target);
    if (!QDir().mkpath(targetInfo.absolutePath())) return false;

    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("VACUUM INTO :target");
    q.bindValue(":target", target);
    if (q.exec()) {
        return true;
    }

    // A plain copy of a WAL-mode database can silently omit recent commits.
    // Checkpoint first before using the compatibility fallback.
    if (!q.exec("PRAGMA wal_checkpoint(FULL)") || !q.next() || q.value(0).toInt() != 0) return false;
    return QFile::copy(m_dbFilePath, target);
}

bool DatabaseManager::createTables() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) return false;

    if (!db.transaction()) return false;
    QSqlQuery q(db);

    bool ok = q.exec(R"(
        CREATE TABLE IF NOT EXISTS systems (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            sort_name TEXT,
            short_name TEXT,
            manufacturer TEXT,
            release_year INTEGER,
            icon_path TEXT,
            notes TEXT,
            created_at TEXT,
            updated_at TEXT,
            sort_order INTEGER DEFAULT 0,
            enabled INTEGER DEFAULT 1
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS scan_roots (
            id TEXT PRIMARY KEY,
            system_id TEXT NOT NULL,
            path TEXT NOT NULL,
            recursive INTEGER DEFAULT 1,
            follow_symlinks INTEGER DEFAULT 0,
            include_extensions TEXT,
            exclude_extensions TEXT,
            exclude_patterns TEXT,
            watch_changes INTEGER DEFAULT 0,
            FOREIGN KEY(system_id) REFERENCES systems(id) ON DELETE CASCADE
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS emulators (
            id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            launch_type INTEGER DEFAULT 0,
            program TEXT NOT NULL,
            working_directory TEXT,
            environment TEXT,
            detach INTEGER DEFAULT 0,
            capture_output INTEGER DEFAULT 1,
            hide_policy INTEGER DEFAULT 0,
            shell_mode INTEGER DEFAULT 0,
            enabled INTEGER DEFAULT 1
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS emulator_arguments (
            id TEXT PRIMARY KEY,
            emulator_id TEXT NOT NULL,
            position INTEGER NOT NULL,
            template TEXT NOT NULL,
            optional INTEGER DEFAULT 0,
            FOREIGN KEY(emulator_id) REFERENCES emulators(id) ON DELETE CASCADE
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS system_emulators (
            system_id TEXT NOT NULL,
            emulator_id TEXT NOT NULL,
            is_default INTEGER DEFAULT 0,
            priority INTEGER DEFAULT 0,
            core_path TEXT,
            supported_extensions TEXT,
            PRIMARY KEY(system_id, emulator_id),
            FOREIGN KEY(system_id) REFERENCES systems(id) ON DELETE CASCADE,
            FOREIGN KEY(emulator_id) REFERENCES emulators(id) ON DELETE CASCADE
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS games (
            id TEXT PRIMARY KEY,
            system_id TEXT NOT NULL,
            title TEXT NOT NULL,
            sort_title TEXT,
            description TEXT,
            release_date TEXT,
            developer TEXT,
            publisher TEXT,
            region TEXT,
            languages TEXT,
            genres TEXT,
            series TEXT,
            players_min INTEGER DEFAULT 1,
            players_max INTEGER DEFAULT 1,
            favorite INTEGER DEFAULT 0,
            user_rating REAL DEFAULT 0.0,
            status TEXT DEFAULT 'Unplayed',
            notes TEXT,
            date_added TEXT,
            last_played TEXT,
            play_count INTEGER DEFAULT 0,
            total_play_seconds INTEGER DEFAULT 0,
            emulator_override_id TEXT,
            missing INTEGER DEFAULT 0,
            FOREIGN KEY(system_id) REFERENCES systems(id) ON DELETE CASCADE
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS game_files (
            id TEXT PRIMARY KEY,
            game_id TEXT NOT NULL,
            path TEXT UNIQUE NOT NULL,
            role INTEGER DEFAULT 0,
            disc_number INTEGER DEFAULT 0,
            file_size INTEGER DEFAULT 0,
            modified_time TEXT,
            crc32 TEXT,
            md5 TEXT,
            sha1 TEXT,
            dat_match_id TEXT,
            available INTEGER DEFAULT 1,
            FOREIGN KEY(game_id) REFERENCES games(id) ON DELETE CASCADE
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS game_media (
            id TEXT PRIMARY KEY,
            game_id TEXT NOT NULL,
            media_type TEXT NOT NULL,
            path TEXT NOT NULL,
            source TEXT,
            width INTEGER DEFAULT 0,
            height INTEGER DEFAULT 0,
            preferred INTEGER DEFAULT 0,
            FOREIGN KEY(game_id) REFERENCES games(id) ON DELETE CASCADE
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS play_sessions (
            id TEXT PRIMARY KEY,
            game_id TEXT NOT NULL,
            emulator_id TEXT,
            started_at TEXT NOT NULL,
            ended_at TEXT,
            duration_seconds INTEGER DEFAULT 0,
            exit_code INTEGER DEFAULT 0,
            exit_status INTEGER DEFAULT 0,
            launch_error TEXT,
            FOREIGN KEY(game_id) REFERENCES games(id) ON DELETE CASCADE
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS active_game_statuses (
            game_id TEXT PRIMARY KEY REFERENCES games(id) ON DELETE CASCADE,
            previous_status TEXT NOT NULL,
            started_at TEXT NOT NULL
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS rom_content_hash (
            rom_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            payload_key TEXT NOT NULL,
            byte_size INTEGER NOT NULL,
            crc32 TEXT,
            md5 TEXT,
            sha1 TEXT,
            sha256 TEXT,
            hash_schema_version INTEGER NOT NULL,
            source_modified_at TEXT,
            computed_at TEXT NOT NULL,
            PRIMARY KEY (rom_id, payload_key)
        );
    )");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS rom_content_hash_fingerprint ON rom_content_hash(rom_id, source_modified_at)");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS games_by_system_title ON games(system_id, title COLLATE NOCASE)");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS game_files_by_game ON game_files(game_id)");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS game_media_by_game ON game_media(game_id, preferred)");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS play_sessions_by_game ON play_sessions(game_id, started_at DESC)");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS rom_metadata_cache (
            rom_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            provider TEXT NOT NULL,
            provider_record_id TEXT,
            normalized_json TEXT,
            raw_response_json TEXT,
            result_kind TEXT NOT NULL,
            identity_confidence TEXT,
            metadata_confidence TEXT,
            matched_hash_algorithm TEXT,
            matched_hash TEXT,
            fetched_at TEXT NOT NULL,
            expires_at TEXT NOT NULL,
            negative_until TEXT,
            last_error_code TEXT,
            last_error_at TEXT,
            PRIMARY KEY (rom_id, provider)
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS dat_sources (
            id TEXT PRIMARY KEY,
            system_id TEXT NOT NULL,
            name TEXT NOT NULL,
            version TEXT,
            author TEXT,
            category TEXT,
            file_path TEXT,
            imported_at TEXT,
            FOREIGN KEY(system_id) REFERENCES systems(id) ON DELETE CASCADE
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS dat_entries (
            id TEXT PRIMARY KEY,
            dat_source_id TEXT NOT NULL,
            game_name TEXT NOT NULL,
            rom_name TEXT NOT NULL,
            size INTEGER DEFAULT 0,
            crc32 TEXT,
            md5 TEXT,
            sha1 TEXT,
            FOREIGN KEY(dat_source_id) REFERENCES dat_sources(id) ON DELETE CASCADE
        );
    )");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS dat_entries_by_crc32 ON dat_entries(crc32)");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS dat_entries_by_sha1 ON dat_entries(sha1)");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS cover_providers (
            id TEXT PRIMARY KEY,
            display_name TEXT NOT NULL,
            adapter_version TEXT NOT NULL,
            credential_mode TEXT NOT NULL CHECK (credential_mode = 'none'),
            stability TEXT NOT NULL,
            enabled INTEGER NOT NULL DEFAULT 1,
            priority INTEGER NOT NULL DEFAULT 0,
            manifest_json TEXT NOT NULL DEFAULT '{}',
            last_success_at TEXT,
            last_failure_at TEXT,
            circuit_state TEXT NOT NULL DEFAULT 'closed',
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS cover_provider_systems (
            provider_id TEXT NOT NULL REFERENCES cover_providers(id) ON DELETE CASCADE,
            system_id TEXT NOT NULL REFERENCES systems(id) ON DELETE CASCADE,
            provider_system_key TEXT NOT NULL,
            provider_system_name TEXT,
            verified INTEGER NOT NULL DEFAULT 0,
            PRIMARY KEY (provider_id, system_id)
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS media_objects (
            sha256 TEXT PRIMARY KEY,
            relative_path TEXT NOT NULL,
            mime_type TEXT NOT NULL,
            extension TEXT NOT NULL,
            byte_size INTEGER NOT NULL,
            width INTEGER NOT NULL,
            height INTEGER NOT NULL,
            perceptual_hash TEXT,
            difference_hash TEXT,
            validation_state TEXT NOT NULL,
            created_at TEXT NOT NULL,
            validated_at TEXT NOT NULL
        );
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS cover_assets (
            id TEXT PRIMARY KEY,
            game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            media_object_sha256 TEXT REFERENCES media_objects(sha256),
            provider_id TEXT NOT NULL REFERENCES cover_providers(id),
            provider_asset_id TEXT,
            provider_game_id TEXT,
            cover_kind TEXT NOT NULL,
            cover_scope TEXT NOT NULL,
            platform_id TEXT,
            region TEXT,
            languages_json TEXT,
            edition TEXT,
            source_page TEXT,
            source_url TEXT,
            provider_title TEXT,
            match_method TEXT NOT NULL,
            match_confidence REAL NOT NULL,
            quality_score REAL,
            final_score REAL,
            rights_status TEXT NOT NULL,
            license_id TEXT,
            license_url TEXT,
            creator TEXT,
            attribution_text TEXT,
            redistribution INTEGER NOT NULL DEFAULT 0,
            preferred INTEGER NOT NULL DEFAULT 0,
            user_selected INTEGER NOT NULL DEFAULT 0,
            user_supplied INTEGER NOT NULL DEFAULT 0,
            locked INTEGER NOT NULL DEFAULT 0,
            downloaded_at TEXT,
            provider_updated_at TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL
        );
    )");

    ok &= q.exec("CREATE INDEX IF NOT EXISTS cover_assets_game ON cover_assets(game_id, cover_kind, preferred)");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS cover_assets_provider ON cover_assets(provider_id, provider_game_id)");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS cover_assets_object ON cover_assets(media_object_sha256)");
    ok &= q.exec(R"(
        CREATE UNIQUE INDEX IF NOT EXISTS one_preferred_front_cover ON cover_assets(game_id)
        WHERE preferred = 1 AND cover_kind IN
        ('box_front', 'jewel_case_front', 'arcade_flyer_front', 'library_vertical_art', 'generated_placeholder')
    )");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS cover_jobs (
            id TEXT PRIMARY KEY,
            game_id TEXT NOT NULL REFERENCES games(id) ON DELETE CASCADE,
            provider_id TEXT REFERENCES cover_providers(id),
            operation TEXT NOT NULL,
            state TEXT NOT NULL,
            priority INTEGER NOT NULL DEFAULT 0,
            request_json TEXT,
            result_json TEXT,
            attempt_count INTEGER NOT NULL DEFAULT 0,
            not_before TEXT,
            last_http_status INTEGER,
            last_error_code TEXT,
            last_error_message TEXT,
            created_at TEXT NOT NULL,
            updated_at TEXT NOT NULL,
            completed_at TEXT
        );
    )");
    ok &= q.exec("CREATE INDEX IF NOT EXISTS cover_job_queue ON cover_jobs(state, not_before, priority DESC, created_at)");

    ok &= q.exec(R"(
        CREATE TABLE IF NOT EXISTS cover_http_cache (
            provider_id TEXT NOT NULL REFERENCES cover_providers(id) ON DELETE CASCADE,
            request_key TEXT NOT NULL,
            status_code INTEGER NOT NULL,
            response_object_sha256 TEXT REFERENCES media_objects(sha256),
            etag TEXT,
            last_modified TEXT,
            fetched_at TEXT NOT NULL,
            expires_at TEXT,
            PRIMARY KEY(provider_id, request_key)
        );
    )");

    if (!ok) {
        db.rollback();
        qCritical() << "Failed to create tables:" << db.lastError().text();
        return false;
    }



    return db.commit();
}

bool DatabaseManager::migrateSchema() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) return false;

    QSqlQuery versionQuery(db);
    if (!versionQuery.exec("PRAGMA user_version")) return false;
    const int previousVersion = versionQuery.next() ? versionQuery.value(0).toInt() : 0;

    // CREATE TABLE IF NOT EXISTS cannot update tables created by an earlier
    // LudoShelf build.  In particular, those builds stored only the basic
    // media-object fields, while the cover pipeline now records validation and
    // provenance data too.  Inspect the live schema rather than relying solely
    // on user_version: an interrupted upgrade may have advanced the version
    // without adding every column.
    QSqlQuery columnsQuery(db);
    if (!columnsQuery.exec("PRAGMA table_info(media_objects)")) {
        qCritical() << "Could not inspect media_objects schema:" << columnsQuery.lastError().text();
        return false;
    }

    QSet<QString> columns;
    while (columnsQuery.next()) columns.insert(columnsQuery.value(1).toString());
    if (columns.isEmpty()) {
        qCritical() << "media_objects table is missing after schema creation.";
        return false;
    }

    const QList<QPair<QString, QString>> requiredColumns{
        {QStringLiteral("extension"), QStringLiteral("TEXT")},
        {QStringLiteral("perceptual_hash"), QStringLiteral("TEXT")},
        {QStringLiteral("difference_hash"), QStringLiteral("TEXT")},
        {QStringLiteral("validation_state"), QStringLiteral("TEXT NOT NULL DEFAULT 'validated'")},
        {QStringLiteral("validated_at"), QStringLiteral("TEXT")},
    };

    if (!db.transaction()) return false;
    QSqlQuery query(db);
    for (const auto& [name, definition] : requiredColumns) {
        if (columns.contains(name)) continue;
        if (!query.exec(QStringLiteral("ALTER TABLE media_objects ADD COLUMN %1 %2").arg(name, definition))) {
            qCritical() << "Could not add media_objects column" << name << ':' << query.lastError().text();
            db.rollback();
            return false;
        }
    }

    // Older builds stored an absolute path despite the column's name.  Keep
    // external paths usable, but normalize paths inside the application data
    // directory to the current portable relative representation.
    QSqlQuery pathsQuery(db);
    if (!pathsQuery.exec("SELECT sha256, relative_path FROM media_objects")) {
        qCritical() << "Could not read existing media object paths:" << pathsQuery.lastError().text();
        db.rollback();
        return false;
    }
    const QDir dataDir(App::AppPaths::dataRoot());
    const QDir legacyDataDir(App::AppPaths::legacyDataRoot());
    const auto relativePathInside = [](const QDir& root, const QString& path) -> QString {
        const QString relative = QDir::cleanPath(root.relativeFilePath(path));
        return relative == "." || (relative != ".." && !relative.startsWith("../")) ? relative : QString();
    };
    QSqlQuery updatePath(db);
    updatePath.prepare("UPDATE media_objects SET relative_path = :relative_path, extension = :extension, "
                       "validation_state = COALESCE(NULLIF(validation_state, ''), 'validated'), "
                       "validated_at = COALESCE(NULLIF(validated_at, ''), :validated_at) WHERE sha256 = :sha256");
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    while (pathsQuery.next()) {
        const QString sha256 = pathsQuery.value(0).toString();
        const QString existingPath = pathsQuery.value(1).toString();
        const QFileInfo fileInfo(existingPath);
        QString relativePath = existingPath;
        if (fileInfo.isAbsolute()) {
            const QString portableRelative = relativePathInside(dataDir, existingPath);
            const QString legacyRelative = relativePathInside(legacyDataDir, existingPath);
            if (!portableRelative.isEmpty()) relativePath = portableRelative;
            else if (!legacyRelative.isEmpty()) relativePath = legacyRelative;
        }
        const QString extension = fileInfo.suffix().toLower().isEmpty()
            ? QStringLiteral("png") : fileInfo.suffix().toLower();
        updatePath.bindValue(":relative_path", relativePath);
        updatePath.bindValue(":extension", extension);
        updatePath.bindValue(":validated_at", now);
        updatePath.bindValue(":sha256", sha256);
        if (!updatePath.exec()) {
            qCritical() << "Could not backfill media object metadata:" << updatePath.lastError().text();
            db.rollback();
            return false;
        }
    }

    // game_media predates cover_assets and stores a full path.  Rewrite old
    // LudoShelf-owned paths too, so its compatibility fallback remains fully
    // portable after the data import.
    QSqlQuery gameMediaQuery(db);
    if (!gameMediaQuery.exec("SELECT id, path FROM game_media")) {
        qCritical() << "Could not read legacy game media paths:" << gameMediaQuery.lastError().text();
        db.rollback();
        return false;
    }
    QSqlQuery updateGameMedia(db);
    updateGameMedia.prepare("UPDATE game_media SET path = :path WHERE id = :id");
    while (gameMediaQuery.next()) {
        const QString id = gameMediaQuery.value(0).toString();
        const QString existingPath = gameMediaQuery.value(1).toString();
        if (!QFileInfo(existingPath).isAbsolute()) continue;
        QString relative = relativePathInside(dataDir, existingPath);
        if (relative.isEmpty()) relative = relativePathInside(legacyDataDir, existingPath);
        if (relative.isEmpty() || relative == ".") continue;
        const QString portablePath = dataDir.filePath(relative);
        if (portablePath == existingPath) continue;
        updateGameMedia.bindValue(":path", portablePath);
        updateGameMedia.bindValue(":id", id);
        if (!updateGameMedia.exec()) {
            qCritical() << "Could not migrate legacy game media path:" << updateGameMedia.lastError().text();
            db.rollback();
            return false;
        }
    }

    // Status is an automatic lifecycle state, not user-entered metadata.
    // Normalize old manual/temporary values once to the current contract.
    if (previousVersion < 4 && !query.exec("UPDATE games SET status = CASE WHEN play_count > 0 THEN 'Played' ELSE 'Unplayed' END")) {
        qCritical() << "Could not normalize automatic game statuses:" << query.lastError().text();
        db.rollback();
        return false;
    }

    // Older versions allowed several defaults for a system. Keep the oldest
    // association deterministically, then make the invariant enforceable.
    if (!query.exec("UPDATE system_emulators SET is_default = 0 WHERE is_default = 1 AND rowid NOT IN "
                    "(SELECT MIN(rowid) FROM system_emulators WHERE is_default = 1 GROUP BY system_id)")) {
        db.rollback();
        return false;
    }
    if (!query.exec("CREATE UNIQUE INDEX IF NOT EXISTS one_default_emulator_per_system "
                    "ON system_emulators(system_id) WHERE is_default = 1")) {
        db.rollback();
        return false;
    }

    if (!query.exec("PRAGMA user_version = 5")) {
        qCritical() << "Could not update database schema version:" << query.lastError().text();
        db.rollback();
        return false;
    }
    return db.commit();
}

bool DatabaseManager::deduplicateCoverAssets() {
    // Earlier cover passes could persist the same downloaded media twice while
    // two requests for a game were in flight.  Retain the preferred/user-picked
    // provenance record and remove only byte-identical provider duplicates.
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    return query.exec(R"(
        DELETE FROM cover_assets
        WHERE id IN (
            SELECT id FROM (
                SELECT id,
                       ROW_NUMBER() OVER (
                           PARTITION BY game_id, provider_id, cover_kind, media_object_sha256
                           ORDER BY preferred DESC, user_selected DESC, created_at ASC, id ASC
                       ) AS ordinal
                FROM cover_assets
                WHERE media_object_sha256 IS NOT NULL AND media_object_sha256 <> ''
            ) WHERE ordinal > 1
        )
    )");
}

QList<Domain::System> DatabaseManager::getSystems() {
    QList<Domain::System> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("SELECT id, name, sort_name, short_name, manufacturer, release_year, icon_path, notes, sort_order, enabled, (SELECT COUNT(*) FROM games WHERE games.system_id = systems.id) as game_count FROM systems ORDER BY sort_order, name", db);

    while (q.next()) {
        Domain::System sys;
        sys.id = QUuid::fromString(q.value(0).toString());
        sys.name = q.value(1).toString();
        sys.sortName = q.value(2).toString();
        sys.shortName = q.value(3).toString();
        sys.manufacturer = q.value(4).toString();
        sys.releaseYear = q.value(5).toInt();
        sys.iconPath = q.value(6).toString();
        sys.notes = q.value(7).toString();
        sys.sortOrder = q.value(8).toInt();
        sys.enabled = q.value(9).toBool();
        sys.gameCount = q.value(10).toInt();
        list.append(sys);
    }
    return list;
}

Domain::System DatabaseManager::getSystem(const QUuid& id) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, name, sort_name, short_name, manufacturer, release_year, icon_path, notes, sort_order, enabled, (SELECT COUNT(*) FROM games WHERE games.system_id = systems.id) as game_count FROM systems WHERE id = :id");
    q.bindValue(":id", id.toString(QUuid::WithBraces));
    if (q.exec() && q.next()) {
        Domain::System sys;
        sys.id = QUuid::fromString(q.value(0).toString());
        sys.name = q.value(1).toString();
        sys.sortName = q.value(2).toString();
        sys.shortName = q.value(3).toString();
        sys.manufacturer = q.value(4).toString();
        sys.releaseYear = q.value(5).toInt();
        sys.iconPath = q.value(6).toString();
        sys.notes = q.value(7).toString();
        sys.sortOrder = q.value(8).toInt();
        sys.enabled = q.value(9).toBool();
        sys.gameCount = q.value(10).toInt();
        return sys;
    }
    return {};
}

bool DatabaseManager::saveSystem(const Domain::System& system) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(R"(
        INSERT INTO systems (id, name, sort_name, short_name, manufacturer, release_year, icon_path, notes, created_at, updated_at, sort_order, enabled)
        VALUES (:id, :name, :sort_name, :short_name, :manufacturer, :release_year, :icon_path, :notes, :created_at, :updated_at, :sort_order, :enabled)
        ON CONFLICT(id) DO UPDATE SET
            name=excluded.name,
            sort_name=excluded.sort_name,
            short_name=excluded.short_name,
            manufacturer=excluded.manufacturer,
            release_year=excluded.release_year,
            icon_path=excluded.icon_path,
            notes=excluded.notes,
            updated_at=excluded.updated_at,
            sort_order=excluded.sort_order,
            enabled=excluded.enabled
    )");
    q.bindValue(":id", system.id.toString(QUuid::WithBraces));
    q.bindValue(":name", system.name);
    q.bindValue(":sort_name", system.sortName);
    q.bindValue(":short_name", system.shortName);
    q.bindValue(":manufacturer", system.manufacturer);
    q.bindValue(":release_year", system.releaseYear);
    q.bindValue(":icon_path", system.iconPath);
    q.bindValue(":notes", system.notes);
    q.bindValue(":created_at", system.createdAt.toString(Qt::ISODate));
    q.bindValue(":updated_at", system.updatedAt.toString(Qt::ISODate));
    q.bindValue(":sort_order", system.sortOrder);
    q.bindValue(":enabled", system.enabled ? 1 : 0);

    return q.exec();
}

bool DatabaseManager::deleteSystem(const QUuid& id) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM systems WHERE id = :id");
    q.bindValue(":id", id.toString(QUuid::WithBraces));
    return q.exec();
}

QList<ScanRoot> DatabaseManager::getScanRoots(const QUuid& systemId) {
    QList<ScanRoot> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, system_id, path, recursive, follow_symlinks, include_extensions, exclude_extensions, exclude_patterns, watch_changes FROM scan_roots WHERE system_id = :system_id");
    q.bindValue(":system_id", systemId.toString(QUuid::WithBraces));

    if (q.exec()) {
        while (q.next()) {
            ScanRoot root;
            root.id = QUuid::fromString(q.value(0).toString());
            root.systemId = QUuid::fromString(q.value(1).toString());
            root.path = q.value(2).toString();
            root.recursive = q.value(3).toBool();
            root.followSymlinks = q.value(4).toBool();
            root.includeExtensions = QJsonDocument::fromJson(q.value(5).toByteArray()).toVariant().toStringList();
            root.excludeExtensions = QJsonDocument::fromJson(q.value(6).toByteArray()).toVariant().toStringList();
            root.excludePatterns = QJsonDocument::fromJson(q.value(7).toByteArray()).toVariant().toStringList();

            root.watchChanges = q.value(8).toBool();
            list.append(root);
        }
    }
    return list;
}

bool DatabaseManager::saveScanRoot(const ScanRoot& root) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(R"(
        INSERT INTO scan_roots (id, system_id, path, recursive, follow_symlinks, include_extensions, exclude_extensions, exclude_patterns, watch_changes)
        VALUES (:id, :system_id, :path, :recursive, :follow_symlinks, :include_extensions, :exclude_extensions, :exclude_patterns, :watch_changes)
        ON CONFLICT(id) DO UPDATE SET
            path=excluded.path,
            recursive=excluded.recursive,
            follow_symlinks=excluded.follow_symlinks,
            include_extensions=excluded.include_extensions,
            exclude_extensions=excluded.exclude_extensions,
            exclude_patterns=excluded.exclude_patterns,
            watch_changes=excluded.watch_changes
    )");
    q.bindValue(":id", root.id.toString(QUuid::WithBraces));
    q.bindValue(":system_id", root.systemId.toString(QUuid::WithBraces));
    q.bindValue(":path", root.path);
    q.bindValue(":recursive", root.recursive ? 1 : 0);
    q.bindValue(":follow_symlinks", root.followSymlinks ? 1 : 0);
    q.bindValue(":include_extensions", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(root.includeExtensions)).toJson(QJsonDocument::Compact)));
    q.bindValue(":exclude_extensions", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(root.excludeExtensions)).toJson(QJsonDocument::Compact)));
    q.bindValue(":exclude_patterns", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(root.excludePatterns)).toJson(QJsonDocument::Compact)));
    q.bindValue(":watch_changes", root.watchChanges ? 1 : 0);

    return q.exec();
}

bool DatabaseManager::deleteScanRoot(const QUuid& id) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM scan_roots WHERE id = :id");
    q.bindValue(":id", id.toString(QUuid::WithBraces));
    return q.exec();
}

QList<Domain::Game> DatabaseManager::getGamesForSystem(const QUuid& systemId) {
    QList<Domain::Game> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, system_id, title, sort_title, description, release_date, developer, publisher, region, languages, genres, series, players_min, players_max, favorite, user_rating, status, notes, date_added, last_played, play_count, total_play_seconds, missing, emulator_override_id FROM games WHERE system_id = :system_id ORDER BY title COLLATE NOCASE ASC, id ASC");
    q.bindValue(":system_id", systemId.toString(QUuid::WithBraces));

    if (q.exec()) {
        while (q.next()) {
            Domain::Game g;
            g.id = QUuid::fromString(q.value(0).toString());
            g.systemId = QUuid::fromString(q.value(1).toString());
            g.title = q.value(2).toString();
            g.sortTitle = q.value(3).toString();
            g.description = q.value(4).toString();
            g.releaseDate = QDate::fromString(q.value(5).toString(), Qt::ISODate);
            g.developer = q.value(6).toString();
            g.publisher = q.value(7).toString();
            g.region = q.value(8).toString();
            g.languages = decodeStringList(q.value(9));
            g.genres = decodeStringList(q.value(10));
            g.series = q.value(11).toString();
            g.playersMin = q.value(12).toInt();
            g.playersMax = q.value(13).toInt();
            g.favorite = q.value(14).toBool();
            g.userRating = q.value(15).toDouble();
            g.status = q.value(16).toString();
            g.notes = q.value(17).toString();
            g.dateAdded = QDateTime::fromString(q.value(18).toString(), Qt::ISODate);
            g.lastPlayed = QDateTime::fromString(q.value(19).toString(), Qt::ISODate);
            g.playCount = q.value(20).toInt();
            g.totalPlaySeconds = q.value(21).toInt();
            g.missing = q.value(22).toBool();
            g.emulatorOverrideId = QUuid::fromString(q.value(23).toString());
            list.append(g);
        }
    }
    return list;
}

QList<Domain::Game> DatabaseManager::getAllGames() {
    QList<Domain::Game> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("SELECT id, system_id, title, sort_title, description, release_date, developer, publisher, region, languages, genres, series, players_min, players_max, favorite, user_rating, status, notes, date_added, last_played, play_count, total_play_seconds, missing, emulator_override_id FROM games ORDER BY title COLLATE NOCASE ASC, id ASC", db);

    while (q.next()) {
        Domain::Game g;
        g.id = QUuid::fromString(q.value(0).toString());
        g.systemId = QUuid::fromString(q.value(1).toString());
        g.title = q.value(2).toString();
        g.sortTitle = q.value(3).toString();
        g.description = q.value(4).toString();
        g.releaseDate = QDate::fromString(q.value(5).toString(), Qt::ISODate);
        g.developer = q.value(6).toString();
        g.publisher = q.value(7).toString();
        g.region = q.value(8).toString();
        g.languages = decodeStringList(q.value(9));
        g.genres = decodeStringList(q.value(10));
        g.series = q.value(11).toString();
        g.playersMin = q.value(12).toInt();
        g.playersMax = q.value(13).toInt();
        g.favorite = q.value(14).toBool();
        g.userRating = q.value(15).toDouble();
        g.status = q.value(16).toString();
        g.notes = q.value(17).toString();
        g.dateAdded = QDateTime::fromString(q.value(18).toString(), Qt::ISODate);
        g.lastPlayed = QDateTime::fromString(q.value(19).toString(), Qt::ISODate);
        g.playCount = q.value(20).toInt();
        g.totalPlaySeconds = q.value(21).toInt();
        g.missing = q.value(22).toBool();
        g.emulatorOverrideId = QUuid::fromString(q.value(23).toString());
        list.append(g);
    }
    return list;
}

Domain::Game DatabaseManager::getGame(const QUuid& id) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, system_id, title, sort_title, description, release_date, developer, publisher, region, languages, genres, series, players_min, players_max, favorite, user_rating, status, notes, date_added, last_played, play_count, total_play_seconds, missing, emulator_override_id FROM games WHERE id = :id");
    q.bindValue(":id", id.toString(QUuid::WithBraces));

    if (q.exec() && q.next()) {
        Domain::Game g;
        g.id = QUuid::fromString(q.value(0).toString());
        g.systemId = QUuid::fromString(q.value(1).toString());
        g.title = q.value(2).toString();
        g.sortTitle = q.value(3).toString();
        g.description = q.value(4).toString();
        g.releaseDate = QDate::fromString(q.value(5).toString(), Qt::ISODate);
        g.developer = q.value(6).toString();
        g.publisher = q.value(7).toString();
        g.region = q.value(8).toString();
        g.languages = decodeStringList(q.value(9));
        g.genres = decodeStringList(q.value(10));
        g.series = q.value(11).toString();
        g.playersMin = q.value(12).toInt();
        g.playersMax = q.value(13).toInt();
        g.favorite = q.value(14).toBool();
        g.userRating = q.value(15).toDouble();
        g.status = q.value(16).toString();
        g.notes = q.value(17).toString();
        g.dateAdded = QDateTime::fromString(q.value(18).toString(), Qt::ISODate);
        g.lastPlayed = QDateTime::fromString(q.value(19).toString(), Qt::ISODate);
        g.playCount = q.value(20).toInt();
        g.totalPlaySeconds = q.value(21).toInt();
        g.missing = q.value(22).toBool();
        g.emulatorOverrideId = QUuid::fromString(q.value(23).toString());
        return g;
    }
    return {};
}

bool DatabaseManager::saveGame(const Domain::Game& game, const Domain::GameFile& primaryFile) {
    QList<QPair<Domain::Game, Domain::GameFile>> batch;
    batch.append({game, primaryFile});
    return saveGamesBatch(batch);
}

bool DatabaseManager::fillGameMetadataFields(const QUuid& gameId, const QDate& releaseDate,
                                             const QString& developer, const QString& region,
                                             const QString& publisher, const QStringList& languages,
                                             const QStringList& genres, const QString& description) {
    if (gameId.isNull()) return false;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    query.prepare(R"(
        UPDATE games SET
            release_date = CASE WHEN TRIM(COALESCE(release_date, '')) = '' AND :release_date != ''
                                THEN :release_date ELSE release_date END,
            developer = CASE WHEN TRIM(COALESCE(developer, '')) = '' AND :developer != ''
                             THEN :developer ELSE developer END,
            region = CASE WHEN TRIM(COALESCE(region, '')) = '' AND :region != ''
                          THEN :region ELSE region END,
            publisher = CASE WHEN TRIM(COALESCE(publisher, '')) = '' AND :publisher != ''
                             THEN :publisher ELSE publisher END,
            languages = CASE WHEN (TRIM(COALESCE(languages, '')) = '' OR languages = '[]') AND :languages != '[]'
                             THEN :languages ELSE languages END,
            genres = CASE WHEN (TRIM(COALESCE(genres, '')) = '' OR genres = '[]') AND :genres != '[]'
                          THEN :genres ELSE genres END,
            description = CASE WHEN TRIM(COALESCE(description, '')) = '' AND :description != ''
                               THEN :description ELSE description END
        WHERE id = :id
    )");
    query.bindValue(":release_date", releaseDate.toString(Qt::ISODate));
    query.bindValue(":developer", developer.trimmed());
    query.bindValue(":region", region.trimmed());
    query.bindValue(":publisher", publisher.trimmed());
    query.bindValue(":languages", encodeStringList(languages));
    query.bindValue(":genres", encodeStringList(genres));
    query.bindValue(":description", description.trimmed());
    query.bindValue(":id", gameId.toString(QUuid::WithBraces));
    return query.exec();
}

bool DatabaseManager::saveGamesBatch(const QList<QPair<Domain::Game, Domain::GameFile>>& gamesList) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;

    QSqlQuery qg(db);
    qg.prepare(R"(
        INSERT INTO games (id, system_id, title, sort_title, description, release_date, developer, publisher, region, languages, genres, series, players_min, players_max, favorite, user_rating, status, notes, date_added, last_played, play_count, total_play_seconds, emulator_override_id, missing)
        VALUES (:id, :system_id, :title, :sort_title, :description, :release_date, :developer, :publisher, :region, :languages, :genres, :series, :players_min, :players_max, :favorite, :user_rating, :status, :notes, :date_added, :last_played, :play_count, :total_play_seconds, :emulator_override_id, :missing)
        ON CONFLICT(id) DO UPDATE SET
            title=excluded.title,
            sort_title=excluded.sort_title,
            description=excluded.description,
            release_date=excluded.release_date,
            developer=excluded.developer,
            publisher=excluded.publisher,
            region=excluded.region,
            languages=excluded.languages,
            genres=excluded.genres,
            series=excluded.series,
            players_min=excluded.players_min,
            players_max=excluded.players_max,
            favorite=excluded.favorite,
            user_rating=excluded.user_rating,
            status=CASE
                WHEN EXISTS (SELECT 1 FROM active_game_statuses WHERE game_id = games.id) THEN 'Playing'
                WHEN excluded.play_count > 0 THEN 'Played'
                ELSE 'Unplayed'
            END,
            notes=excluded.notes,
            last_played=excluded.last_played,
            play_count=excluded.play_count,
            total_play_seconds=excluded.total_play_seconds,
            emulator_override_id=excluded.emulator_override_id,
            missing=excluded.missing
    )");

    QSqlQuery qf(db);
    qf.prepare(R"(
        INSERT INTO game_files (id, game_id, path, role, disc_number, file_size, modified_time, crc32, md5, sha1, available)
        VALUES (:id, :game_id, :path, :role, :disc_number, :file_size, :modified_time, :crc32, :md5, :sha1, :available)
        ON CONFLICT(path) DO UPDATE SET
            game_id=excluded.game_id,
            file_size=excluded.file_size,
            modified_time=excluded.modified_time,
            crc32=excluded.crc32,
            md5=excluded.md5,
            sha1=excluded.sha1,
            available=excluded.available
    )");

    for (const auto& item : gamesList) {
        const auto& game = item.first;
        const auto& file = item.second;

        qg.bindValue(":id", game.id.toString(QUuid::WithBraces));
        qg.bindValue(":system_id", game.systemId.toString(QUuid::WithBraces));
        qg.bindValue(":title", game.title);
        qg.bindValue(":sort_title", game.sortTitle);
        qg.bindValue(":description", game.description);
        qg.bindValue(":release_date", game.releaseDate.toString(Qt::ISODate));
        qg.bindValue(":developer", game.developer);
        qg.bindValue(":publisher", game.publisher);
        qg.bindValue(":region", game.region);
        qg.bindValue(":languages", encodeStringList(game.languages));
        qg.bindValue(":genres", encodeStringList(game.genres));
        qg.bindValue(":series", game.series);
        qg.bindValue(":players_min", game.playersMin);
        qg.bindValue(":players_max", game.playersMax);
        qg.bindValue(":favorite", game.favorite ? 1 : 0);
        qg.bindValue(":user_rating", game.userRating);
        qg.bindValue(":status", game.playCount > 0 ? QStringLiteral("Played") : QStringLiteral("Unplayed"));
        qg.bindValue(":notes", game.notes);
        qg.bindValue(":date_added", game.dateAdded.toString(Qt::ISODate));
        qg.bindValue(":last_played", game.lastPlayed.toString(Qt::ISODate));
        qg.bindValue(":play_count", game.playCount);
        qg.bindValue(":total_play_seconds", game.totalPlaySeconds);
        qg.bindValue(":emulator_override_id", game.emulatorOverrideId.isNull() ? QString() : game.emulatorOverrideId.toString(QUuid::WithBraces));
        qg.bindValue(":missing", game.missing ? 1 : 0);

        if (!qg.exec()) {
            db.rollback();
            qCritical() << "Batch game save failed:" << qg.lastError().text();
            return false;
        }

        if (!file.path.isEmpty()) {
            qf.bindValue(":id", file.id.toString(QUuid::WithBraces));
            qf.bindValue(":game_id", game.id.toString(QUuid::WithBraces));
            qf.bindValue(":path", file.path);
            qf.bindValue(":role", static_cast<int>(file.role));
            qf.bindValue(":disc_number", file.discNumber);
            qf.bindValue(":file_size", file.fileSize);
            qf.bindValue(":modified_time", file.modifiedTime.toString(Qt::ISODate));
            qf.bindValue(":crc32", file.crc32);
            qf.bindValue(":md5", file.md5);
            qf.bindValue(":sha1", file.sha1);
            qf.bindValue(":available", file.available ? 1 : 0);

            if (!qf.exec()) {
                db.rollback();
                qCritical() << "Batch game file save failed:" << qf.lastError().text();
                return false;
            }
        }
    }

    return db.commit();
}

bool DatabaseManager::updateScannedGame(const QUuid& gameId, const Domain::Game& scannedGame,
                                        const Domain::GameFile& primaryFile) {
    if (gameId.isNull()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;
    QSqlQuery gameQuery(db);
    gameQuery.prepare("UPDATE games SET title = :title, sort_title = :sort_title, region = :region, missing = 0 WHERE id = :id");
    gameQuery.bindValue(":title", scannedGame.title);
    gameQuery.bindValue(":sort_title", scannedGame.sortTitle);
    gameQuery.bindValue(":region", scannedGame.region);
    gameQuery.bindValue(":id", gameId.toString(QUuid::WithBraces));
    if (!gameQuery.exec() || gameQuery.numRowsAffected() != 1) { db.rollback(); return false; }
    if (!primaryFile.path.isEmpty()) {
        QSqlQuery fileQuery(db);
        fileQuery.prepare("UPDATE game_files SET file_size = :file_size, modified_time = :modified_time, available = :available WHERE path = :path");
        fileQuery.bindValue(":file_size", primaryFile.fileSize);
        fileQuery.bindValue(":modified_time", primaryFile.modifiedTime.toString(Qt::ISODate));
        fileQuery.bindValue(":available", primaryFile.available ? 1 : 0);
        fileQuery.bindValue(":path", primaryFile.path);
        if (!fileQuery.exec()) { db.rollback(); return false; }
    }
    return db.commit();
}

bool DatabaseManager::deleteGame(const QUuid& id) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM games WHERE id = :id");
    q.bindValue(":id", id.toString(QUuid::WithBraces));
    return q.exec();
}

QList<Domain::GameFile> DatabaseManager::getFilesForGame(const QUuid& gameId) {
    QList<Domain::GameFile> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, game_id, path, role, disc_number, file_size, modified_time, crc32, md5, sha1, dat_match_id, available FROM game_files WHERE game_id = :game_id");
    q.bindValue(":game_id", gameId.toString(QUuid::WithBraces));

    if (q.exec()) {
        while (q.next()) {
            Domain::GameFile gf;
            gf.id = QUuid::fromString(q.value(0).toString());
            gf.gameId = QUuid::fromString(q.value(1).toString());
            gf.path = q.value(2).toString();
            gf.role = static_cast<Domain::FileRole>(q.value(3).toInt());
            gf.discNumber = q.value(4).toInt();
            gf.fileSize = q.value(5).toLongLong();
            gf.modifiedTime = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
            gf.crc32 = q.value(7).toString();
            gf.md5 = q.value(8).toString();
            gf.sha1 = q.value(9).toString();
            gf.datMatchId = QUuid::fromString(q.value(10).toString());
            gf.available = q.value(11).toBool();
            list.append(gf);
        }
    }
    return list;
}

Domain::GameFile DatabaseManager::getPrimaryFileForGame(const QUuid& gameId) {
    auto files = getFilesForGame(gameId);
    for (const auto& f : files) {
        if (f.role == Domain::FileRole::Primary) return f;
    }
    return files.isEmpty() ? Domain::GameFile{} : files.first();
}

bool DatabaseManager::updateFileAvailability(const QUuid& fileId, bool available) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("UPDATE game_files SET available = :available WHERE id = :id");
    q.bindValue(":available", available ? 1 : 0);
    q.bindValue(":id", fileId.toString(QUuid::WithBraces));
    return q.exec();
}

bool DatabaseManager::reconcileScannedFiles(const QUuid& systemId, const QSet<QString>& observedAbsolutePaths) {
    if (systemId.isNull()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;

    QSqlQuery files(db);
    files.prepare("SELECT game_files.id, game_files.path FROM game_files "
                  "JOIN games ON games.id = game_files.game_id WHERE games.system_id = :system_id");
    files.bindValue(":system_id", systemId.toString(QUuid::WithBraces));
    if (!files.exec()) { db.rollback(); return false; }

    QSqlQuery setAvailability(db);
    setAvailability.prepare("UPDATE game_files SET available = :available WHERE id = :id");
    while (files.next()) {
        const QString storedPath = QFileInfo(files.value(1).toString()).absoluteFilePath();
        const bool available = observedAbsolutePaths.contains(storedPath);
        setAvailability.bindValue(":available", available ? 1 : 0);
        setAvailability.bindValue(":id", files.value(0).toString());
        if (!setAvailability.exec()) { db.rollback(); return false; }
    }

    QSqlQuery updateGames(db);
    updateGames.prepare(R"(
        UPDATE games SET missing = NOT EXISTS (
            SELECT 1 FROM game_files WHERE game_files.game_id = games.id AND game_files.available = 1
        ) WHERE system_id = :system_id
    )");
    updateGames.bindValue(":system_id", systemId.toString(QUuid::WithBraces));
    if (!updateGames.exec()) { db.rollback(); return false; }
    return db.commit();
}

bool DatabaseManager::updateFileHashes(const QUuid& fileId, const QString& crc32, const QString& md5, const QString& sha1,
                                       const QUuid& datMatchId) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("UPDATE game_files SET crc32 = :crc32, md5 = :md5, sha1 = :sha1, dat_match_id = :dat_match_id WHERE id = :id");
    q.bindValue(":crc32", crc32);
    q.bindValue(":md5", md5);
    q.bindValue(":sha1", sha1);
    q.bindValue(":dat_match_id", datMatchId.isNull() ? QString() : datMatchId.toString(QUuid::WithBraces));
    q.bindValue(":id", fileId.toString(QUuid::WithBraces));
    return q.exec();
}

QList<GameMedia> DatabaseManager::getMediaForGame(const QUuid& gameId) {
    QList<GameMedia> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, game_id, media_type, path, source, width, height, preferred FROM game_media WHERE game_id = :game_id");
    q.bindValue(":game_id", gameId.toString(QUuid::WithBraces));

    if (q.exec()) {
        while (q.next()) {
            GameMedia m;
            m.id = QUuid::fromString(q.value(0).toString());
            m.gameId = QUuid::fromString(q.value(1).toString());
            m.mediaType = q.value(2).toString();
            m.path = q.value(3).toString();
            m.source = q.value(4).toString();
            m.width = q.value(5).toInt();
            m.height = q.value(6).toInt();
            m.preferred = q.value(7).toBool();
            list.append(m);
        }
    }
    return list;
}

bool DatabaseManager::saveGameMedia(const GameMedia& media) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(R"(
        INSERT INTO game_media (id, game_id, media_type, path, source, width, height, preferred)
        VALUES (:id, :game_id, :media_type, :path, :source, :width, :height, :preferred)
        ON CONFLICT(id) DO UPDATE SET
            media_type=excluded.media_type,
            path=excluded.path,
            source=excluded.source,
            width=excluded.width,
            height=excluded.height,
            preferred=excluded.preferred
    )");
    q.bindValue(":id", media.id.toString(QUuid::WithBraces));
    q.bindValue(":game_id", media.gameId.toString(QUuid::WithBraces));
    q.bindValue(":media_type", media.mediaType);
    q.bindValue(":path", media.path);
    q.bindValue(":source", media.source);
    q.bindValue(":width", media.width);
    q.bindValue(":height", media.height);
    q.bindValue(":preferred", media.preferred ? 1 : 0);

    return q.exec();
}

bool DatabaseManager::saveCoverProvider(const Covers::CoverProvider& provider) {
    if (provider.id.isEmpty() || provider.credentialMode != "none") return false;
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.prepare(R"(
        INSERT INTO cover_providers
        (id, display_name, adapter_version, credential_mode, stability, enabled, priority, manifest_json,
         last_success_at, last_failure_at, circuit_state, created_at, updated_at)
        VALUES (:id, :display_name, :adapter_version, :credential_mode, :stability, :enabled, :priority,
                :manifest_json, :last_success_at, :last_failure_at, :circuit_state, :created_at, :updated_at)
        ON CONFLICT(id) DO UPDATE SET
            display_name=excluded.display_name, adapter_version=excluded.adapter_version,
            credential_mode=excluded.credential_mode, stability=excluded.stability, enabled=excluded.enabled,
            priority=excluded.priority, manifest_json=excluded.manifest_json,
            last_success_at=excluded.last_success_at, last_failure_at=excluded.last_failure_at,
            circuit_state=excluded.circuit_state, updated_at=excluded.updated_at
    )");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    q.bindValue(":id", provider.id);
    q.bindValue(":display_name", provider.displayName);
    q.bindValue(":adapter_version", provider.adapterVersion);
    q.bindValue(":credential_mode", provider.credentialMode);
    q.bindValue(":stability", provider.stability);
    q.bindValue(":enabled", provider.enabled ? 1 : 0);
    q.bindValue(":priority", provider.priority);
    q.bindValue(":manifest_json", provider.manifestJson.isEmpty() ? QStringLiteral("{}") : provider.manifestJson);
    q.bindValue(":last_success_at", provider.lastSuccessAt.toString(Qt::ISODate));
    q.bindValue(":last_failure_at", provider.lastFailureAt.toString(Qt::ISODate));
    q.bindValue(":circuit_state", provider.circuitState);
    q.bindValue(":created_at", now.toString(Qt::ISODate));
    q.bindValue(":updated_at", now.toString(Qt::ISODate));
    return q.exec();
}

Covers::CoverProvider DatabaseManager::getCoverProvider(const QString& providerId) {
    Covers::CoverProvider provider;
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.prepare("SELECT id, display_name, adapter_version, credential_mode, stability, enabled, priority, manifest_json, last_success_at, last_failure_at, circuit_state FROM cover_providers WHERE id = :id");
    q.bindValue(":id", providerId);
    if (!q.exec() || !q.next()) return provider;
    provider.id = q.value(0).toString();
    provider.displayName = q.value(1).toString();
    provider.adapterVersion = q.value(2).toString();
    provider.credentialMode = q.value(3).toString();
    provider.stability = q.value(4).toString();
    provider.enabled = q.value(5).toBool();
    provider.priority = q.value(6).toInt();
    provider.manifestJson = q.value(7).toString();
    provider.lastSuccessAt = QDateTime::fromString(q.value(8).toString(), Qt::ISODate);
    provider.lastFailureAt = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
    provider.circuitState = q.value(10).toString();
    return provider;
}

bool DatabaseManager::saveMediaObject(const Covers::MediaObject& object) {
    if (object.sha256.size() != 64 || object.relativePath.isEmpty()) return false;
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.prepare(R"(
        INSERT INTO media_objects (sha256, relative_path, mime_type, extension, byte_size, width, height,
                                   perceptual_hash, difference_hash, validation_state, created_at, validated_at)
        VALUES (:sha256, :relative_path, :mime_type, :extension, :byte_size, :width, :height,
                :perceptual_hash, :difference_hash, :validation_state, :created_at, :validated_at)
        ON CONFLICT(sha256) DO UPDATE SET
            relative_path=excluded.relative_path, mime_type=excluded.mime_type, extension=excluded.extension,
            byte_size=excluded.byte_size, width=excluded.width, height=excluded.height,
            perceptual_hash=excluded.perceptual_hash, difference_hash=excluded.difference_hash,
            validation_state=excluded.validation_state, validated_at=excluded.validated_at
    )");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    q.bindValue(":sha256", object.sha256);
    q.bindValue(":relative_path", object.relativePath);
    q.bindValue(":mime_type", object.mimeType);
    q.bindValue(":extension", object.extension);
    q.bindValue(":byte_size", object.byteSize);
    q.bindValue(":width", object.width);
    q.bindValue(":height", object.height);
    q.bindValue(":perceptual_hash", object.perceptualHash);
    q.bindValue(":difference_hash", object.differenceHash);
    q.bindValue(":validation_state", object.validationState);
    q.bindValue(":created_at", (object.createdAt.isValid() ? object.createdAt : now).toString(Qt::ISODate));
    q.bindValue(":validated_at", (object.validatedAt.isValid() ? object.validatedAt : now).toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "Failed to save media object" << object.sha256 << ':' << q.lastError().text();
        return false;
    }
    return true;
}

Covers::MediaObject DatabaseManager::getMediaObject(const QString& sha256) {
    Covers::MediaObject object;
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.prepare("SELECT sha256, relative_path, mime_type, extension, byte_size, width, height, perceptual_hash, difference_hash, validation_state, created_at, validated_at FROM media_objects WHERE sha256 = :sha256");
    q.bindValue(":sha256", sha256);
    if (!q.exec() || !q.next()) return object;
    object.sha256 = q.value(0).toString(); object.relativePath = q.value(1).toString();
    object.mimeType = q.value(2).toString(); object.extension = q.value(3).toString();
    object.byteSize = q.value(4).toLongLong(); object.width = q.value(5).toInt(); object.height = q.value(6).toInt();
    object.perceptualHash = q.value(7).toString(); object.differenceHash = q.value(8).toString();
    object.validationState = q.value(9).toString();
    object.createdAt = QDateTime::fromString(q.value(10).toString(), Qt::ISODate);
    object.validatedAt = QDateTime::fromString(q.value(11).toString(), Qt::ISODate);
    return object;
}

bool DatabaseManager::saveCoverAsset(const Covers::CoverAsset& asset) {
    if (asset.id.isNull() || asset.gameId.isNull() || asset.providerId.isEmpty()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;
    // Downloads can overlap when a user starts a second cover pass before the
    // first has completed.  The same provider image should update its existing
    // provenance record, not create another hidden duplicate.
    QUuid assetId = asset.id;
    QSqlQuery duplicate(db);
    duplicate.prepare(asset.mediaObjectSha256.isEmpty() ? R"(
        SELECT id FROM cover_assets
        WHERE game_id = :game_id AND provider_id = :provider_id AND cover_kind = :cover_kind
          AND COALESCE(media_object_sha256, '') = ''
        ORDER BY preferred DESC, created_at ASC LIMIT 1
    )" : R"(
        SELECT id FROM cover_assets
        WHERE game_id = :game_id AND provider_id = :provider_id AND cover_kind = :cover_kind
          AND media_object_sha256 = :media_object_sha256
        ORDER BY preferred DESC, created_at ASC LIMIT 1
    )");
    duplicate.bindValue(":game_id", asset.gameId.toString(QUuid::WithBraces));
    duplicate.bindValue(":provider_id", asset.providerId);
    duplicate.bindValue(":cover_kind", Covers::coverKindToString(asset.kind));
    if (!asset.mediaObjectSha256.isEmpty()) duplicate.bindValue(":media_object_sha256", asset.mediaObjectSha256);
    if (duplicate.exec() && duplicate.next()) assetId = QUuid::fromString(duplicate.value(0).toString());
    QSqlQuery q(db);
    if (asset.preferred && Covers::isPrimaryDisplayCover(asset.kind)) {
        q.prepare("UPDATE cover_assets SET preferred = 0 WHERE game_id = :game_id AND cover_kind IN ('box_front', 'jewel_case_front', 'arcade_flyer_front', 'library_vertical_art', 'generated_placeholder')");
        q.bindValue(":game_id", asset.gameId.toString(QUuid::WithBraces));
        if (!q.exec()) { db.rollback(); return false; }
    }
    q.prepare(R"(
        INSERT INTO cover_assets
        (id, game_id, media_object_sha256, provider_id, provider_asset_id, provider_game_id, cover_kind, cover_scope,
         platform_id, region, languages_json, edition, source_page, source_url, provider_title, match_method,
         match_confidence, quality_score, final_score, rights_status, license_id, license_url, creator,
         attribution_text, redistribution, preferred, user_selected, user_supplied, locked, downloaded_at,
         provider_updated_at, created_at, updated_at)
        VALUES (:id, :game_id, :media_object_sha256, :provider_id, :provider_asset_id, :provider_game_id,
                :cover_kind, :cover_scope, :platform_id, :region, :languages_json, :edition, :source_page,
                :source_url, :provider_title, :match_method, :match_confidence, :quality_score, :final_score,
                :rights_status, :license_id, :license_url, :creator, :attribution_text, :redistribution,
                :preferred, :user_selected, :user_supplied, :locked, :downloaded_at, :provider_updated_at,
                :created_at, :updated_at)
        ON CONFLICT(id) DO UPDATE SET
            media_object_sha256=excluded.media_object_sha256, provider_id=excluded.provider_id,
            provider_asset_id=excluded.provider_asset_id, provider_game_id=excluded.provider_game_id,
            cover_kind=excluded.cover_kind, cover_scope=excluded.cover_scope, platform_id=excluded.platform_id,
            region=excluded.region, languages_json=excluded.languages_json, edition=excluded.edition,
            source_page=excluded.source_page, source_url=excluded.source_url, provider_title=excluded.provider_title,
            match_method=excluded.match_method, match_confidence=excluded.match_confidence,
            quality_score=excluded.quality_score, final_score=excluded.final_score,
            rights_status=excluded.rights_status, license_id=excluded.license_id, license_url=excluded.license_url,
            creator=excluded.creator, attribution_text=excluded.attribution_text, redistribution=excluded.redistribution,
            preferred=excluded.preferred, user_selected=excluded.user_selected, user_supplied=excluded.user_supplied,
            locked=excluded.locked, downloaded_at=excluded.downloaded_at,
            provider_updated_at=excluded.provider_updated_at, updated_at=excluded.updated_at
    )");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    q.bindValue(":id", assetId.toString(QUuid::WithBraces)); q.bindValue(":game_id", asset.gameId.toString(QUuid::WithBraces));
    q.bindValue(":media_object_sha256", asset.mediaObjectSha256); q.bindValue(":provider_id", asset.providerId);
    q.bindValue(":provider_asset_id", asset.providerAssetId); q.bindValue(":provider_game_id", asset.providerGameId);
    q.bindValue(":cover_kind", Covers::coverKindToString(asset.kind)); q.bindValue(":cover_scope", Covers::coverScopeToString(asset.scope));
    q.bindValue(":platform_id", asset.platformId); q.bindValue(":region", asset.region);
    q.bindValue(":languages_json", QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(asset.languages)).toJson(QJsonDocument::Compact)));
    q.bindValue(":edition", asset.edition); q.bindValue(":source_page", asset.sourcePage.toString()); q.bindValue(":source_url", asset.sourceUrl.toString());
    q.bindValue(":provider_title", asset.providerTitle); q.bindValue(":match_method", asset.matchMethod);
    q.bindValue(":match_confidence", asset.matchConfidence); q.bindValue(":quality_score", asset.qualityScore); q.bindValue(":final_score", asset.finalScore);
    q.bindValue(":rights_status", asset.rightsStatus); q.bindValue(":license_id", asset.licenseId); q.bindValue(":license_url", asset.licenseUrl.toString());
    q.bindValue(":creator", asset.creator); q.bindValue(":attribution_text", asset.attribution);
    q.bindValue(":redistribution", asset.redistributionAllowed ? 1 : 0); q.bindValue(":preferred", asset.preferred ? 1 : 0);
    q.bindValue(":user_selected", asset.userSelected ? 1 : 0); q.bindValue(":user_supplied", asset.userSupplied ? 1 : 0); q.bindValue(":locked", asset.locked ? 1 : 0);
    q.bindValue(":downloaded_at", asset.downloadedAt.toString(Qt::ISODate)); q.bindValue(":provider_updated_at", asset.providerUpdatedAt.toString(Qt::ISODate));
    q.bindValue(":created_at", asset.createdAt.toString(Qt::ISODate)); q.bindValue(":updated_at", now.toString(Qt::ISODate));
    if (!q.exec()) { db.rollback(); return false; }
    return db.commit();
}

QList<Covers::CoverAsset> DatabaseManager::getCoverAssetsForGame(const QUuid& gameId) {
    QList<Covers::CoverAsset> assets;
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.prepare(R"(
        SELECT id, game_id, media_object_sha256, provider_id, provider_asset_id, provider_game_id, cover_kind,
               cover_scope, platform_id, region, languages_json, edition, source_page, source_url, provider_title,
               match_method, match_confidence, quality_score, final_score, rights_status, license_id, license_url,
               creator, attribution_text, redistribution, preferred, user_selected, user_supplied, locked,
               downloaded_at, provider_updated_at, created_at, updated_at
        FROM cover_assets WHERE game_id = :game_id
        ORDER BY preferred DESC, user_selected DESC, final_score DESC, created_at DESC
    )");
    q.bindValue(":game_id", gameId.toString(QUuid::WithBraces));
    if (!q.exec()) return assets;
    while (q.next()) {
        Covers::CoverAsset a;
        a.id = QUuid::fromString(q.value(0).toString()); a.gameId = QUuid::fromString(q.value(1).toString());
        a.mediaObjectSha256 = q.value(2).toString(); a.providerId = q.value(3).toString();
        a.providerAssetId = q.value(4).toString(); a.providerGameId = q.value(5).toString();
        a.kind = Covers::coverKindFromString(q.value(6).toString()); a.scope = Covers::coverScopeFromString(q.value(7).toString());
        a.platformId = q.value(8).toString(); a.region = q.value(9).toString();
        a.languages = QJsonDocument::fromJson(q.value(10).toByteArray()).toVariant().toStringList(); a.edition = q.value(11).toString();
        a.sourcePage = QUrl(q.value(12).toString()); a.sourceUrl = QUrl(q.value(13).toString()); a.providerTitle = q.value(14).toString();
        a.matchMethod = q.value(15).toString(); a.matchConfidence = q.value(16).toDouble();
        a.qualityScore = q.value(17).toDouble(); a.finalScore = q.value(18).toDouble(); a.rightsStatus = q.value(19).toString();
        a.licenseId = q.value(20).toString(); a.licenseUrl = QUrl(q.value(21).toString()); a.creator = q.value(22).toString();
        a.attribution = q.value(23).toString(); a.redistributionAllowed = q.value(24).toBool(); a.preferred = q.value(25).toBool();
        a.userSelected = q.value(26).toBool(); a.userSupplied = q.value(27).toBool(); a.locked = q.value(28).toBool();
        a.downloadedAt = QDateTime::fromString(q.value(29).toString(), Qt::ISODate);
        a.providerUpdatedAt = QDateTime::fromString(q.value(30).toString(), Qt::ISODate);
        a.createdAt = QDateTime::fromString(q.value(31).toString(), Qt::ISODate);
        a.updatedAt = QDateTime::fromString(q.value(32).toString(), Qt::ISODate);
        assets.append(a);
    }
    return assets;
}

Covers::CoverAsset DatabaseManager::getPreferredCoverAsset(const QUuid& gameId) {
    const auto assets = getCoverAssetsForGame(gameId);
    for (const auto& asset : assets) if (asset.preferred) return asset;
    return {};
}

QHash<QUuid, QString> DatabaseManager::getPreferredCoverObjectHashes() {
    QHash<QUuid, QString> result;
    QSqlQuery query(QSqlDatabase::database(m_connectionName));
    if (!query.exec("SELECT game_id, media_object_sha256 FROM cover_assets WHERE preferred = 1 "
                    "AND media_object_sha256 IS NOT NULL AND media_object_sha256 <> ''")) return result;
    while (query.next()) result.insert(QUuid::fromString(query.value(0).toString()), query.value(1).toString());
    return result;
}

bool DatabaseManager::setPreferredCoverAsset(const QUuid& gameId, const QUuid& assetId) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;
    QSqlQuery q(db);
    q.prepare("UPDATE cover_assets SET preferred = 0 WHERE game_id = :game_id AND cover_kind IN ('box_front', 'jewel_case_front', 'arcade_flyer_front', 'library_vertical_art', 'generated_placeholder')");
    q.bindValue(":game_id", gameId.toString(QUuid::WithBraces));
    if (!q.exec()) { db.rollback(); return false; }
    q.prepare("UPDATE cover_assets SET preferred = 1, user_selected = 1, locked = 1, updated_at = :updated_at WHERE id = :id AND game_id = :game_id");
    q.bindValue(":id", assetId.toString(QUuid::WithBraces)); q.bindValue(":game_id", gameId.toString(QUuid::WithBraces));
    q.bindValue(":updated_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!q.exec() || q.numRowsAffected() != 1) { db.rollback(); return false; }
    return db.commit();
}

bool DatabaseManager::saveCoverJob(const Covers::CoverJob& job) {
    if (job.id.isNull() || job.gameId.isNull() || job.operation.isEmpty()) return false;
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.prepare(R"(
        INSERT INTO cover_jobs (id, game_id, provider_id, operation, state, priority, request_json, result_json,
                                attempt_count, not_before, last_http_status, last_error_code, last_error_message,
                                created_at, updated_at, completed_at)
        VALUES (:id, :game_id, :provider_id, :operation, :state, :priority, :request_json, :result_json,
                :attempt_count, :not_before, :last_http_status, :last_error_code, :last_error_message,
                :created_at, :updated_at, :completed_at)
        ON CONFLICT(id) DO UPDATE SET state=excluded.state, priority=excluded.priority,
            request_json=excluded.request_json, result_json=excluded.result_json,
            attempt_count=excluded.attempt_count, not_before=excluded.not_before,
            last_http_status=excluded.last_http_status, last_error_code=excluded.last_error_code,
            last_error_message=excluded.last_error_message, updated_at=excluded.updated_at,
            completed_at=excluded.completed_at
    )");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    q.bindValue(":id", job.id.toString(QUuid::WithBraces)); q.bindValue(":game_id", job.gameId.toString(QUuid::WithBraces));
    q.bindValue(":provider_id", job.providerId); q.bindValue(":operation", job.operation); q.bindValue(":state", job.state);
    q.bindValue(":priority", job.priority); q.bindValue(":request_json", job.requestJson); q.bindValue(":result_json", job.resultJson);
    q.bindValue(":attempt_count", job.attemptCount); q.bindValue(":not_before", job.notBefore.toString(Qt::ISODate));
    q.bindValue(":last_http_status", job.lastHttpStatus); q.bindValue(":last_error_code", job.lastErrorCode);
    q.bindValue(":last_error_message", job.lastErrorMessage); q.bindValue(":created_at", job.createdAt.toString(Qt::ISODate));
    q.bindValue(":updated_at", now.toString(Qt::ISODate)); q.bindValue(":completed_at", job.completedAt.toString(Qt::ISODate));
    return q.exec();
}

QList<Covers::CoverJob> DatabaseManager::getRunnableCoverJobs(const QDateTime& now, int limit) {
    QList<Covers::CoverJob> jobs;
    QSqlQuery q(QSqlDatabase::database(m_connectionName));
    q.prepare(R"(
        SELECT id, game_id, provider_id, operation, state, priority, request_json, result_json, attempt_count,
               not_before, last_http_status, last_error_code, last_error_message, created_at, updated_at, completed_at
        FROM cover_jobs WHERE state IN ('created', 'retry')
          AND (not_before IS NULL OR not_before = '' OR not_before <= :now)
        ORDER BY priority DESC, created_at LIMIT :limit
    )");
    q.bindValue(":now", now.toString(Qt::ISODate)); q.bindValue(":limit", qMax(1, limit));
    if (!q.exec()) return jobs;
    while (q.next()) {
        Covers::CoverJob j;
        j.id = QUuid::fromString(q.value(0).toString()); j.gameId = QUuid::fromString(q.value(1).toString());
        j.providerId = q.value(2).toString(); j.operation = q.value(3).toString(); j.state = q.value(4).toString();
        j.priority = q.value(5).toInt(); j.requestJson = q.value(6).toString(); j.resultJson = q.value(7).toString();
        j.attemptCount = q.value(8).toInt(); j.notBefore = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
        j.lastHttpStatus = q.value(10).toInt(); j.lastErrorCode = q.value(11).toString(); j.lastErrorMessage = q.value(12).toString();
        j.createdAt = QDateTime::fromString(q.value(13).toString(), Qt::ISODate); j.updatedAt = QDateTime::fromString(q.value(14).toString(), Qt::ISODate);
        j.completedAt = QDateTime::fromString(q.value(15).toString(), Qt::ISODate); jobs.append(j);
    }
    return jobs;
}

QList<Domain::EmulatorProfile> DatabaseManager::getEmulators() {
    QList<Domain::EmulatorProfile> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q("SELECT id, name, launch_type, program, working_directory, environment, detach, capture_output, hide_policy, shell_mode, enabled FROM emulators ORDER BY name", db);

    while (q.next()) {
        Domain::EmulatorProfile emu;
        emu.id = QUuid::fromString(q.value(0).toString());
        emu.name = q.value(1).toString();
        emu.launchType = static_cast<Domain::LaunchType>(q.value(2).toInt());
        emu.program = q.value(3).toString();
        emu.workingDirectory = q.value(4).toString();
        const QJsonDocument environmentDocument = QJsonDocument::fromJson(q.value(5).toByteArray());
        if (environmentDocument.isObject()) {
            const QJsonObject environmentObject = environmentDocument.object();
            for (auto it = environmentObject.constBegin(); it != environmentObject.constEnd(); ++it)
                emu.environment.insert(it.key(), it.value().toString());
        }
        emu.detach = q.value(6).toBool();
        emu.captureOutput = q.value(7).toBool();
        emu.hidePolicy = static_cast<Domain::HidePolicy>(q.value(8).toInt());
        emu.shellMode = q.value(9).toBool();
        emu.enabled = q.value(10).toBool();

        // Load Arguments
        QSqlQuery qa(db);
        qa.prepare("SELECT id, position, template, optional FROM emulator_arguments WHERE emulator_id = :emu_id ORDER BY position");
        qa.bindValue(":emu_id", emu.id.toString(QUuid::WithBraces));
        if (qa.exec()) {
            while (qa.next()) {
                Domain::ArgumentTemplate arg;
                arg.id = QUuid::fromString(qa.value(0).toString());
                arg.position = qa.value(1).toInt();
                arg.templateString = qa.value(2).toString();
                arg.optional = qa.value(3).toBool();
                emu.arguments.append(arg);
            }
        }

        list.append(emu);
    }
    return list;
}

Domain::EmulatorProfile DatabaseManager::getEmulator(const QUuid& id) {
    auto emus = getEmulators();
    for (const auto& e : emus) {
        if (e.id == id) return e;
    }
    return {};
}

bool DatabaseManager::saveEmulator(const Domain::EmulatorProfile& emulator) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;

    QSqlQuery q(db);
    q.prepare(R"(
        INSERT INTO emulators (id, name, launch_type, program, working_directory, environment, detach, capture_output, hide_policy, shell_mode, enabled)
        VALUES (:id, :name, :launch_type, :program, :working_directory, :environment, :detach, :capture_output, :hide_policy, :shell_mode, :enabled)
        ON CONFLICT(id) DO UPDATE SET
            name=excluded.name,
            launch_type=excluded.launch_type,
            program=excluded.program,
            working_directory=excluded.working_directory,
            environment=excluded.environment,
            detach=excluded.detach,
            capture_output=excluded.capture_output,
            hide_policy=excluded.hide_policy,
            shell_mode=excluded.shell_mode,
            enabled=excluded.enabled
    )");
    q.bindValue(":id", emulator.id.toString(QUuid::WithBraces));
    q.bindValue(":name", emulator.name);
    q.bindValue(":launch_type", static_cast<int>(emulator.launchType));
    q.bindValue(":program", emulator.program);
    q.bindValue(":working_directory", emulator.workingDirectory);
    QJsonObject environmentObject;
    for (auto it = emulator.environment.cbegin(); it != emulator.environment.cend(); ++it)
        environmentObject.insert(it.key(), it.value());
    q.bindValue(":environment", QJsonDocument(environmentObject).toJson(QJsonDocument::Compact));
    q.bindValue(":detach", emulator.detach ? 1 : 0);
    q.bindValue(":capture_output", emulator.captureOutput ? 1 : 0);
    q.bindValue(":hide_policy", static_cast<int>(emulator.hidePolicy));
    q.bindValue(":shell_mode", emulator.shellMode ? 1 : 0);
    q.bindValue(":enabled", emulator.enabled ? 1 : 0);

    if (!q.exec()) {
        db.rollback();
        return false;
    }

    // Replace arguments
    QSqlQuery qdel(db);
    qdel.prepare("DELETE FROM emulator_arguments WHERE emulator_id = :emu_id");
    qdel.bindValue(":emu_id", emulator.id.toString(QUuid::WithBraces));
    if (!qdel.exec()) {
        db.rollback();
        return false;
    }

    QSqlQuery qarg(db);
    qarg.prepare("INSERT INTO emulator_arguments (id, emulator_id, position, template, optional) VALUES (:id, :emu_id, :pos, :tpl, :opt)");
    for (int i = 0; i < emulator.arguments.size(); ++i) {
        const auto& arg = emulator.arguments[i];
        qarg.bindValue(":id", arg.id.toString(QUuid::WithBraces));
        qarg.bindValue(":emu_id", emulator.id.toString(QUuid::WithBraces));
        qarg.bindValue(":pos", i);
        qarg.bindValue(":tpl", arg.templateString);
        qarg.bindValue(":opt", arg.optional ? 1 : 0);
        if (!qarg.exec()) {
            db.rollback();
            return false;
        }
    }

    return db.commit();
}

bool DatabaseManager::deleteEmulator(const QUuid& id) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("DELETE FROM emulators WHERE id = :id");
    q.bindValue(":id", id.toString(QUuid::WithBraces));
    return q.exec();
}

bool DatabaseManager::setSystemDefaultEmulator(const QUuid& systemId, const QUuid& emulatorId) {
    if (systemId.isNull() || emulatorId.isNull()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;
    QSqlQuery q(db);
    q.prepare("UPDATE system_emulators SET is_default = 0 WHERE system_id = :sys_id");
    q.bindValue(":sys_id", systemId.toString(QUuid::WithBraces));
    if (!q.exec()) { db.rollback(); return false; }
    q.prepare(R"(
        INSERT INTO system_emulators (system_id, emulator_id, is_default)
        VALUES (:sys_id, :emu_id, 1)
        ON CONFLICT(system_id, emulator_id) DO UPDATE SET is_default = 1
    )");
    q.bindValue(":sys_id", systemId.toString(QUuid::WithBraces));
    q.bindValue(":emu_id", emulatorId.toString(QUuid::WithBraces));
    if (!q.exec()) { db.rollback(); return false; }
    return db.commit();
}

QUuid DatabaseManager::getSystemDefaultEmulator(const QUuid& systemId) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT emulator_id FROM system_emulators WHERE system_id = :sys_id AND is_default = 1 ORDER BY priority DESC, emulator_id LIMIT 1");
    q.bindValue(":sys_id", systemId.toString(QUuid::WithBraces));
    if (q.exec() && q.next()) {
        return QUuid::fromString(q.value(0).toString());
    }
    return {};
}

bool DatabaseManager::savePlaySession(const Domain::PlaySession& session) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare(R"(
        INSERT INTO play_sessions (id, game_id, emulator_id, started_at, ended_at, duration_seconds, exit_code, exit_status, launch_error)
        VALUES (:id, :game_id, :emulator_id, :started_at, :ended_at, :duration_seconds, :exit_code, :exit_status, :launch_error)
    )");
    q.bindValue(":id", session.id.toString(QUuid::WithBraces));
    q.bindValue(":game_id", session.gameId.toString(QUuid::WithBraces));
    q.bindValue(":emulator_id", session.emulatorId.isNull() ? QString() : session.emulatorId.toString(QUuid::WithBraces));
    q.bindValue(":started_at", session.startedAt.toString(Qt::ISODate));
    q.bindValue(":ended_at", session.endedAt.toString(Qt::ISODate));
    q.bindValue(":duration_seconds", session.durationSeconds);
    q.bindValue(":exit_code", session.exitCode);
    q.bindValue(":exit_status", session.exitStatus);
    q.bindValue(":launch_error", session.launchError);

    return q.exec();
}

bool DatabaseManager::markGameLaunching(const QUuid& gameId) {
    if (gameId.isNull()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;

    QSqlQuery currentStatus(db);
    currentStatus.prepare("SELECT status FROM games WHERE id = :game_id");
    currentStatus.bindValue(":game_id", gameId.toString(QUuid::WithBraces));
    if (!currentStatus.exec() || !currentStatus.next()) {
        db.rollback();
        return false;
    }

    QSqlQuery activeStatus(db);
    activeStatus.prepare("INSERT OR IGNORE INTO active_game_statuses (game_id, previous_status, started_at) "
                         "VALUES (:game_id, :previous_status, :started_at)");
    activeStatus.bindValue(":game_id", gameId.toString(QUuid::WithBraces));
    activeStatus.bindValue(":previous_status", currentStatus.value(0).toString());
    activeStatus.bindValue(":started_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!activeStatus.exec()) {
        qWarning() << "Could not mark game as launching:" << activeStatus.lastError().text();
        db.rollback();
        return false;
    }

    QSqlQuery setPlaying(db);
    setPlaying.prepare("UPDATE games SET status = 'Playing' WHERE id = :game_id");
    setPlaying.bindValue(":game_id", gameId.toString(QUuid::WithBraces));
    if (!setPlaying.exec()) {
        qWarning() << "Could not set temporary playing status:" << setPlaying.lastError().text();
        db.rollback();
        return false;
    }
    return db.commit();
}

bool DatabaseManager::recordCompletedPlay(const Domain::PlaySession& session) {
    if (session.gameId.isNull()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;

    const QDateTime endedAt = session.endedAt.isValid() ? session.endedAt : QDateTime::currentDateTimeUtc();
    const int duration = qMax(0, session.durationSeconds);
    QSqlQuery sessionQuery(db);
    sessionQuery.prepare(R"(
        INSERT INTO play_sessions (id, game_id, emulator_id, started_at, ended_at, duration_seconds, exit_code, exit_status, launch_error)
        VALUES (:id, :game_id, :emulator_id, :started_at, :ended_at, :duration_seconds, :exit_code, :exit_status, :launch_error)
    )");
    sessionQuery.bindValue(":id", session.id.toString(QUuid::WithBraces));
    sessionQuery.bindValue(":game_id", session.gameId.toString(QUuid::WithBraces));
    sessionQuery.bindValue(":emulator_id", session.emulatorId.isNull() ? QString() : session.emulatorId.toString(QUuid::WithBraces));
    sessionQuery.bindValue(":started_at", session.startedAt.toString(Qt::ISODate));
    sessionQuery.bindValue(":ended_at", endedAt.toString(Qt::ISODate));
    sessionQuery.bindValue(":duration_seconds", duration);
    sessionQuery.bindValue(":exit_code", session.exitCode);
    sessionQuery.bindValue(":exit_status", session.exitStatus);
    sessionQuery.bindValue(":launch_error", session.launchError);
    if (!sessionQuery.exec()) {
        qWarning() << "Could not save completed play session:" << sessionQuery.lastError().text();
        db.rollback();
        return false;
    }

    QSqlQuery gameQuery(db);
    gameQuery.prepare(R"(
        UPDATE games SET
            last_played = :last_played,
            play_count = play_count + 1,
            total_play_seconds = total_play_seconds + :duration,
            status = 'Played'
        WHERE id = :game_id
    )");
    gameQuery.bindValue(":last_played", endedAt.toString(Qt::ISODate));
    gameQuery.bindValue(":duration", duration);
    gameQuery.bindValue(":game_id", session.gameId.toString(QUuid::WithBraces));
    if (!gameQuery.exec() || gameQuery.numRowsAffected() != 1) {
        qWarning() << "Could not update game play statistics:" << gameQuery.lastError().text();
        db.rollback();
        return false;
    }
    QSqlQuery removeActiveStatus(db);
    removeActiveStatus.prepare("DELETE FROM active_game_statuses WHERE game_id = :game_id");
    removeActiveStatus.bindValue(":game_id", session.gameId.toString(QUuid::WithBraces));
    if (!removeActiveStatus.exec()) {
        db.rollback();
        return false;
    }
    return db.commit();
}

bool DatabaseManager::recordDetachedLaunch(const Domain::PlaySession& session) {
    if (session.gameId.isNull()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;
    QSqlQuery active(db);
    active.prepare("SELECT previous_status FROM active_game_statuses WHERE game_id = :game_id");
    active.bindValue(":game_id", session.gameId.toString(QUuid::WithBraces));
    if (!active.exec() || !active.next()) { db.rollback(); return false; }
    const QString previousStatus = active.value(0).toString();
    QSqlQuery addSession(db);
    addSession.prepare("INSERT INTO play_sessions (id, game_id, emulator_id, started_at, ended_at, duration_seconds, exit_code, exit_status, launch_error) "
                       "VALUES (:id, :game_id, :emulator_id, :started_at, :ended_at, 0, 0, -1, :launch_error)");
    addSession.bindValue(":id", session.id.toString(QUuid::WithBraces));
    addSession.bindValue(":game_id", session.gameId.toString(QUuid::WithBraces));
    addSession.bindValue(":emulator_id", session.emulatorId.isNull() ? QString() : session.emulatorId.toString(QUuid::WithBraces));
    addSession.bindValue(":started_at", session.startedAt.toString(Qt::ISODate));
    addSession.bindValue(":ended_at", session.endedAt.isValid() ? session.endedAt.toString(Qt::ISODate) : QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    addSession.bindValue(":launch_error", QStringLiteral("Detached launch: completion could not be observed."));
    if (!addSession.exec()) { db.rollback(); return false; }
    QSqlQuery restore(db);
    restore.prepare("UPDATE games SET status = :status WHERE id = :game_id");
    restore.bindValue(":status", previousStatus);
    restore.bindValue(":game_id", session.gameId.toString(QUuid::WithBraces));
    if (!restore.exec()) { db.rollback(); return false; }
    QSqlQuery clear(db);
    clear.prepare("DELETE FROM active_game_statuses WHERE game_id = :game_id");
    clear.bindValue(":game_id", session.gameId.toString(QUuid::WithBraces));
    if (!clear.exec()) { db.rollback(); return false; }
    return db.commit();
}

bool DatabaseManager::restoreInterruptedGameStatuses() {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;
    QSqlQuery activeStatuses(db);
    if (!activeStatuses.exec("SELECT game_id, started_at FROM active_game_statuses")) {
        db.rollback();
        return false;
    }
    QSqlQuery restoreStatus(db);
    restoreStatus.prepare("UPDATE games SET status = 'Played', play_count = play_count + 1, "
                          "total_play_seconds = total_play_seconds + :duration WHERE id = :game_id");
    QSqlQuery removeStatus(db);
    removeStatus.prepare("DELETE FROM active_game_statuses WHERE game_id = :game_id");
    while (activeStatuses.next()) {
        const QString gameId = activeStatuses.value(0).toString();
        // If LudoShelf exits while an emulator runs, the emulator cannot be
        // tracked after the next startup.  A successful process start counts
        // as a play, so recover it as Played rather than leaving Playing stale.
        const QDateTime startedAt = QDateTime::fromString(activeStatuses.value(1).toString(), Qt::ISODate);
        restoreStatus.bindValue(":duration", startedAt.isValid() ? qMax(0, static_cast<int>(startedAt.secsTo(QDateTime::currentDateTimeUtc()))) : 0);
        restoreStatus.bindValue(":game_id", gameId);
        if (!restoreStatus.exec()) {
            db.rollback();
            return false;
        }
        removeStatus.bindValue(":game_id", gameId);
        if (!removeStatus.exec()) {
            db.rollback();
            return false;
        }
    }
    QSqlQuery normalizeStatus(db);
    if (!normalizeStatus.exec("UPDATE games SET status = CASE WHEN play_count > 0 THEN 'Played' ELSE 'Unplayed' END WHERE status = 'Playing'")) {
        db.rollback();
        return false;
    }
    return db.commit();
}

QList<Domain::PlaySession> DatabaseManager::getPlaySessionsForGame(const QUuid& gameId) {
    QList<Domain::PlaySession> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, game_id, emulator_id, started_at, ended_at, duration_seconds, exit_code, exit_status, launch_error FROM play_sessions WHERE game_id = :game_id ORDER BY started_at DESC");
    q.bindValue(":game_id", gameId.toString(QUuid::WithBraces));

    if (q.exec()) {
        while (q.next()) {
            Domain::PlaySession ps;
            ps.id = QUuid::fromString(q.value(0).toString());
            ps.gameId = QUuid::fromString(q.value(1).toString());
            ps.emulatorId = QUuid::fromString(q.value(2).toString());
            ps.startedAt = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
            ps.endedAt = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
            ps.durationSeconds = q.value(5).toInt();
            ps.exitCode = q.value(6).toInt();
            ps.exitStatus = q.value(7).toInt();
            ps.launchError = q.value(8).toString();
            list.append(ps);
        }
    }
    return list;
}

bool DatabaseManager::saveDatSource(const DatSource& source, const QList<DatEntry>& entries) {
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.transaction()) return false;

    QUuid sourceId = source.id;
    // Re-importing the same file is an update, not a second competing source.
    if (!source.filePath.trimmed().isEmpty()) {
        QSqlQuery existing(db);
        existing.prepare("SELECT id FROM dat_sources WHERE system_id = :system_id AND file_path = :file_path LIMIT 1");
        existing.bindValue(":system_id", source.systemId.toString(QUuid::WithBraces));
        existing.bindValue(":file_path", source.filePath);
        if (!existing.exec()) { db.rollback(); return false; }
        if (existing.next()) {
            sourceId = QUuid::fromString(existing.value(0).toString());
            QSqlQuery clearEntries(db);
            clearEntries.prepare("DELETE FROM dat_entries WHERE dat_source_id = :source_id");
            clearEntries.bindValue(":source_id", sourceId.toString(QUuid::WithBraces));
            if (!clearEntries.exec()) { db.rollback(); return false; }
        }
    }

    QSqlQuery q(db);
    q.prepare(R"(
        INSERT INTO dat_sources (id, system_id, name, version, author, category, file_path, imported_at)
        VALUES (:id, :system_id, :name, :version, :author, :category, :file_path, :imported_at)
        ON CONFLICT(id) DO UPDATE SET
            name=excluded.name,
            version=excluded.version,
            author=excluded.author,
            category=excluded.category,
            file_path=excluded.file_path,
            imported_at=excluded.imported_at
    )");
    q.bindValue(":id", sourceId.toString(QUuid::WithBraces));
    q.bindValue(":system_id", source.systemId.toString(QUuid::WithBraces));
    q.bindValue(":name", source.name);
    q.bindValue(":version", source.version);
    q.bindValue(":author", source.author);
    q.bindValue(":category", source.category);
    q.bindValue(":file_path", source.filePath);
    q.bindValue(":imported_at", source.importedAt.toString(Qt::ISODate));

    if (!q.exec()) {
        db.rollback();
        return false;
    }

    QSqlQuery qe(db);
    qe.prepare(R"(
        INSERT INTO dat_entries (id, dat_source_id, game_name, rom_name, size, crc32, md5, sha1)
        VALUES (:id, :dat_source_id, :game_name, :rom_name, :size, :crc32, :md5, :sha1)
    )");

    for (const auto& entry : entries) {
        qe.bindValue(":id", entry.id.toString(QUuid::WithBraces));
        qe.bindValue(":dat_source_id", sourceId.toString(QUuid::WithBraces));
        qe.bindValue(":game_name", entry.gameName);
        qe.bindValue(":rom_name", entry.romName);
        qe.bindValue(":size", entry.size);
        qe.bindValue(":crc32", entry.crc32.toLower());
        qe.bindValue(":md5", entry.md5.toLower());
        qe.bindValue(":sha1", entry.sha1.toLower());

        if (!qe.exec()) {
            db.rollback();
            return false;
        }
    }

    return db.commit();
}

QList<DatSource> DatabaseManager::getDatSources(const QUuid& systemId) {
    QList<DatSource> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, system_id, name, version, author, category, file_path, imported_at FROM dat_sources WHERE system_id = :system_id");
    q.bindValue(":system_id", systemId.toString(QUuid::WithBraces));

    if (q.exec()) {
        while (q.next()) {
            DatSource s;
            s.id = QUuid::fromString(q.value(0).toString());
            s.systemId = QUuid::fromString(q.value(1).toString());
            s.name = q.value(2).toString();
            s.version = q.value(3).toString();
            s.author = q.value(4).toString();
            s.category = q.value(5).toString();
            s.filePath = q.value(6).toString();
            s.importedAt = QDateTime::fromString(q.value(7).toString(), Qt::ISODate);
            list.append(s);
        }
    }
    return list;
}

QList<DatEntry> DatabaseManager::getDatEntriesForSource(const QUuid& sourceId) {
    QList<DatEntry> list;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    q.prepare("SELECT id, dat_source_id, game_name, rom_name, size, crc32, md5, sha1 FROM dat_entries WHERE dat_source_id = :source_id");
    q.bindValue(":source_id", sourceId.toString(QUuid::WithBraces));

    if (q.exec()) {
        while (q.next()) {
            DatEntry e;
            e.id = QUuid::fromString(q.value(0).toString());
            e.datSourceId = QUuid::fromString(q.value(1).toString());
            e.gameName = q.value(2).toString();
            e.romName = q.value(3).toString();
            e.size = q.value(4).toLongLong();
            e.crc32 = q.value(5).toString();
            e.md5 = q.value(6).toString();
            e.sha1 = q.value(7).toString();
            list.append(e);
        }
    }
    return list;
}

bool DatabaseManager::matchDatEntry(const QUuid& systemId, const QString& crc32, const QString& sha1, DatEntry& matchedEntry) {
    if (systemId.isNull()) return false;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    QSqlQuery q(db);
    if (!sha1.isEmpty()) {
        q.prepare("SELECT dat_entries.id, dat_entries.dat_source_id, dat_entries.game_name, dat_entries.rom_name, dat_entries.size, dat_entries.crc32, dat_entries.md5, dat_entries.sha1 "
                  "FROM dat_entries JOIN dat_sources ON dat_sources.id = dat_entries.dat_source_id "
                  "WHERE dat_sources.system_id = :system_id AND dat_entries.sha1 = :sha1 ORDER BY dat_sources.imported_at DESC LIMIT 1");
        q.bindValue(":sha1", sha1.toLower());
    } else if (!crc32.isEmpty()) {
        q.prepare("SELECT dat_entries.id, dat_entries.dat_source_id, dat_entries.game_name, dat_entries.rom_name, dat_entries.size, dat_entries.crc32, dat_entries.md5, dat_entries.sha1 "
                  "FROM dat_entries JOIN dat_sources ON dat_sources.id = dat_entries.dat_source_id "
                  "WHERE dat_sources.system_id = :system_id AND dat_entries.crc32 = :crc32 ORDER BY dat_sources.imported_at DESC LIMIT 1");
        q.bindValue(":crc32", crc32.toLower());
    } else {
        return false;
    }
    q.bindValue(":system_id", systemId.toString(QUuid::WithBraces));

    if (q.exec() && q.next()) {
        matchedEntry.id = QUuid::fromString(q.value(0).toString());
        matchedEntry.datSourceId = QUuid::fromString(q.value(1).toString());
        matchedEntry.gameName = q.value(2).toString();
        matchedEntry.romName = q.value(3).toString();
        matchedEntry.size = q.value(4).toLongLong();
        matchedEntry.crc32 = q.value(5).toString();
        matchedEntry.md5 = q.value(6).toString();
        matchedEntry.sha1 = q.value(7).toString();
        return true;
    }
    return false;
}


} // namespace LudoShelf::Database
