#include "LibraryBackupService.h"

#include "AppPaths.h"
#include "../database/DatabaseManager.h"
#include "../media/MediaStorageManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSqlQuery>
#include <QSqlRecord>

namespace LudoShelf::App {
namespace {

const QStringList kTables{
    "systems", "scan_roots", "emulators", "emulator_arguments", "system_emulators",
    "games", "game_files", "game_media", "play_sessions", "active_game_statuses",
    "rom_content_hash", "rom_metadata_cache", "dat_sources", "dat_entries",
    "cover_providers", "cover_provider_systems", "media_objects", "cover_assets",
    "cover_jobs", "cover_http_cache"
};

QJsonArray tableRows(const QString& table) {
    QJsonArray rows;
    QSqlQuery query(Database::DatabaseManager::instance().connection());
    if (!query.exec(QStringLiteral("SELECT * FROM %1").arg(table))) return rows;
    const QSqlRecord record = query.record();
    while (query.next()) {
        QJsonObject row;
        for (int column = 0; column < record.count(); ++column) {
            const QVariant value = query.value(column);
            row.insert(record.fieldName(column), value.isNull() ? QJsonValue() : QJsonValue::fromVariant(value));
        }
        rows.append(row);
    }
    return rows;
}

bool importTableRows(QSqlDatabase db, const QString& table, const QJsonArray& rows) {
    for (const QJsonValue& value : rows) {
        const QJsonObject row = value.toObject();
        if (row.isEmpty()) continue;
        QStringList columns;
        QStringList placeholders;
        QList<QVariant> values;
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            columns.append(QStringLiteral("\"%1\"").arg(it.key()));
            placeholders.append("?");
            values.append(it.value().isNull() ? QVariant() : it.value().toVariant());
        }
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT OR REPLACE INTO %1 (%2) VALUES (%3)")
            .arg(table, columns.join(','), placeholders.join(',')));
        for (const QVariant& field : values) query.addBindValue(field);
        if (!query.exec()) return false;
    }
    return true;
}

QString mediaPath(const QJsonObject& object) {
    const QString path = object.value("relative_path").toString();
    if (QFileInfo(path).isAbsolute()) return path;
    return QDir(AppPaths::dataRoot()).filePath(path);
}

} // namespace

bool LibraryBackupService::exportLibraryToJson(const QString& targetJsonPath) {
    QJsonObject root;
    root["format"] = "ludoshelf-library-snapshot";
    root["version"] = 2;
    root["exported_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QJsonObject tables;
    for (const QString& table : kTables) tables.insert(table, tableRows(table));
    root["tables"] = tables;

    // Media objects are content-addressed, so embedding the bytes gives a
    // portable JSON library snapshot without trusting the original machine's
    // absolute media paths.
    QJsonArray media;
    for (const QJsonValue& value : tables.value("media_objects").toArray()) {
        const QJsonObject object = value.toObject();
        QFile file(mediaPath(object));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const QByteArray bytes = file.readAll();
        const QString expected = object.value("sha256").toString().toLower();
        const QString actual = QString::fromUtf8(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        if (expected.size() != 64 || actual != expected) continue;
        QJsonObject entry;
        entry["sha256"] = expected;
        entry["extension"] = object.value("extension").toString();
        entry["bytes_base64"] = QString::fromLatin1(bytes.toBase64());
        media.append(entry);
    }
    root["media_objects"] = media;

    QSaveFile file(targetJsonPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0) return false;
    return file.commit();
}

bool LibraryBackupService::importLibraryFromJson(const QString& sourceJsonPath) {
    QFile file(sourceJsonPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return false;
    const QJsonObject root = document.object();
    if (root.value("format").toString() != "ludoshelf-library-snapshot" || root.value("version").toInt() != 2) return false;
    const QJsonObject tables = root.value("tables").toObject();
    if (tables.isEmpty()) return false;

    QSqlDatabase db = Database::DatabaseManager::instance().connection();
    if (!db.transaction()) return false;
    for (const QString& table : kTables) {
        if (!importTableRows(db, table, tables.value(table).toArray())) {
            db.rollback();
            return false;
        }
    }
    if (!db.commit()) return false;

    for (const QJsonValue& value : root.value("media_objects").toArray()) {
        const QJsonObject entry = value.toObject();
        const QString sha256 = entry.value("sha256").toString().toLower();
        const QString extension = entry.value("extension").toString().toLower();
        const QByteArray bytes = QByteArray::fromBase64(entry.value("bytes_base64").toString().toLatin1());
        if (sha256.size() != 64 || extension.isEmpty() || bytes.isEmpty() ||
            QString::fromUtf8(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != sha256) return false;
        const QString destination = Media::MediaStorageManager::instance().getObjectAbsolutePath(sha256, extension);
        if (QFile::exists(destination)) continue;
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return false;
    }
    return true;
}

} // namespace LudoShelf::App
