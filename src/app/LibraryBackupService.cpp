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
#include <QSet>

namespace LudoShelf::App {
namespace {

const QStringList kTables{
    "systems", "scan_roots", "emulators", "emulator_arguments", "system_emulators",
    "games", "game_files", "game_media", "play_sessions", "active_game_statuses",
    "rom_content_hash", "rom_metadata_cache", "dat_sources", "dat_entries",
    "cover_providers", "cover_provider_systems", "media_objects", "cover_assets",
    "cover_jobs", "cover_http_cache"
};

bool tableRows(const QString& table, QJsonArray& rows) {
    rows = {};
    QSqlQuery query(Database::DatabaseManager::instance().connection());
    if (!query.exec(QStringLiteral("SELECT * FROM %1").arg(table))) return false;
    const QSqlRecord record = query.record();
    while (query.next()) {
        QJsonObject row;
        for (int column = 0; column < record.count(); ++column) {
            const QVariant value = query.value(column);
            row.insert(record.fieldName(column), value.isNull() ? QJsonValue() : QJsonValue::fromVariant(value));
        }
        rows.append(row);
    }
    return true;
}

bool importTableRows(QSqlDatabase db, const QString& table, const QJsonArray& rows) {
    QSet<QString> allowedColumns;
    QSqlQuery schema(db);
    if (!schema.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) return false;
    while (schema.next()) allowedColumns.insert(schema.value(1).toString());
    if (allowedColumns.isEmpty()) return false;
    for (const QJsonValue& value : rows) {
        if (!value.isObject()) return false;
        const QJsonObject row = value.toObject();
        if (row.isEmpty()) continue;
        QStringList columns;
        QStringList placeholders;
        QList<QVariant> values;
        for (auto it = row.constBegin(); it != row.constEnd(); ++it) {
            if (!allowedColumns.contains(it.key())) return false;
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

bool validSha256(const QString& value) {
    if (value.size() != 64) return false;
    for (const QChar character : value) if (!character.isDigit() && (character < 'a' || character > 'f')) return false;
    return true;
}

bool validExtension(const QString& value) {
    static const QSet<QString> allowed{"png", "jpg", "jpeg", "webp", "bmp", "avif"};
    return allowed.contains(value.toLower());
}

bool writeValidatedMediaObjects(const QJsonArray& media) {
    QSet<QString> seen;
    for (const QJsonValue& value : media) {
        if (!value.isObject()) return false;
        const QJsonObject entry = value.toObject();
        const QString sha256 = entry.value("sha256").toString().toLower();
        const QString extension = entry.value("extension").toString().toLower();
        const QByteArray bytes = QByteArray::fromBase64(entry.value("bytes_base64").toString().toLatin1());
        if (!validSha256(sha256) || !validExtension(extension) || bytes.isEmpty() || seen.contains(sha256) ||
            QString::fromUtf8(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()) != sha256) return false;
        seen.insert(sha256);
        const QString destination = Media::MediaStorageManager::instance().getObjectAbsolutePath(sha256, extension);
        if (destination.isEmpty()) return false;
        if (QFile::exists(destination)) {
            QFile existing(destination);
            if (!existing.open(QIODevice::ReadOnly) ||
                QString::fromUtf8(QCryptographicHash::hash(existing.readAll(), QCryptographicHash::Sha256).toHex()) != sha256) return false;
            continue;
        }
        QSaveFile output(destination);
        if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() || !output.commit()) return false;
    }
    return true;
}

bool normalizeImportedMediaRows(QJsonObject& tables) {
    QJsonArray objects = tables.value("media_objects").toArray();
    for (int index = 0; index < objects.size(); ++index) {
        if (!objects.at(index).isObject()) return false;
        QJsonObject object = objects.at(index).toObject();
        const QString sha256 = object.value("sha256").toString().toLower();
        const QString extension = object.value("extension").toString().toLower();
        if (!validSha256(sha256) || !validExtension(extension)) return false;
        object["sha256"] = sha256;
        object["extension"] = extension;
        object["relative_path"] = QStringLiteral("media/objects/%1/%2/%3.%4")
            .arg(sha256.left(2), sha256.mid(2, 2), sha256, extension);
        objects[index] = object;
    }
    tables["media_objects"] = objects;
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
    for (const QString& table : kTables) {
        QJsonArray rows;
        if (!tableRows(table, rows)) return false;
        tables.insert(table, rows);
    }
    root["tables"] = tables;

    // Media objects are content-addressed, so embedding the bytes gives a
    // portable JSON library snapshot without trusting the original machine's
    // absolute media paths.
    QJsonArray media;
    for (const QJsonValue& value : tables.value("media_objects").toArray()) {
        const QJsonObject object = value.toObject();
        QFile file(mediaPath(object));
        if (!file.open(QIODevice::ReadOnly)) return false;
        const QByteArray bytes = file.readAll();
        const QString expected = object.value("sha256").toString().toLower();
        const QString actual = QString::fromUtf8(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        if (!validSha256(expected) || actual != expected || !validExtension(object.value("extension").toString())) return false;
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

bool LibraryBackupService::importLibraryFromJson(const QString& sourceJsonPath, bool replaceExisting) {
    QFile file(sourceJsonPath);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return false;
    const QJsonObject root = document.object();
    if (root.value("format").toString() != "ludoshelf-library-snapshot" || root.value("version").toInt() != 2) return false;
    QJsonObject tables = root.value("tables").toObject();
    if (tables.isEmpty()) return false;
    for (const QString& table : kTables) if (!tables.value(table).isArray()) return false;
    const QJsonArray media = root.value("media_objects").toArray();
    if (!root.value("media_objects").isArray() || !normalizeImportedMediaRows(tables) || !writeValidatedMediaObjects(media)) return false;

    QSqlDatabase db = Database::DatabaseManager::instance().connection();
    if (!db.transaction()) return false;
    QSqlQuery pragmaQuery(db);
    if (replaceExisting) {
        for (auto it = kTables.crbegin(); it != kTables.crend(); ++it) {
            if (!pragmaQuery.exec(QStringLiteral("DELETE FROM %1").arg(*it))) {
                db.rollback();
                return false;
            }
        }
    }
    for (const QString& table : kTables) {
        if (!importTableRows(db, table, tables.value(table).toArray())) {
            db.rollback();
            return false;
        }
    }
    pragmaQuery.exec("PRAGMA foreign_keys = ON;");
    QSqlQuery integrity(db);
    if (!integrity.exec("PRAGMA foreign_key_check;") || integrity.next()) {
        db.rollback();
        return false;
    }
    return db.commit();
}

} // namespace LudoShelf::App
