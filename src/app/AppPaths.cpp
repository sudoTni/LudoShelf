#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QDebug>

namespace LudoShelf::App {
namespace {

bool ensureDirectory(const QString& path) {
    return QDir().mkpath(path);
}

bool copyDirectoryContents(const QString& sourcePath, const QString& targetPath) {
    const QDir source(sourcePath);
    if (!source.exists()) return true;
    if (!ensureDirectory(targetPath)) return false;

    const QFileInfoList entries = source.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        const QString sourceFile = entry.absoluteFilePath();
        const QString targetFile = QDir(targetPath).filePath(entry.fileName());
        if (entry.isSymLink()) continue;
        if (entry.isDir()) {
            if (!copyDirectoryContents(sourceFile, targetFile)) return false;
            continue;
        }
        if (entry.fileName() == "ludoshelf.db" || entry.fileName() == "ludoshelf.db-wal" ||
            entry.fileName() == "ludoshelf.db-shm" || entry.fileName() == "ludoshelf.db-journal") {
            continue;
        }
        if (!QFile::exists(targetFile) && !QFile::copy(sourceFile, targetFile)) return false;
    }
    return true;
}

bool copyDatabaseSnapshot(const QString& sourcePath, const QString& targetPath) {
    const QString connectionName = "ludoshelf_legacy_import";
    if (QSqlDatabase::contains(connectionName)) QSqlDatabase::removeDatabase(connectionName);

    bool copied = false;
    {
        QSqlDatabase legacy = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        legacy.setDatabaseName(sourcePath);
        if (legacy.open()) {
            QSqlQuery query(legacy);
            QString escapedTarget = targetPath;
            escapedTarget.replace('\'', "''");
            copied = query.exec(QStringLiteral("VACUUM INTO '%1'").arg(escapedTarget));
            if (!copied) qWarning() << "Could not create portable library snapshot:" << query.lastError().text();
            legacy.close();
        } else {
            qWarning() << "Could not open legacy library for portable import:" << legacy.lastError().text();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return copied;
}

void copyLegacySettingsIfNeeded() {
    if (QFileInfo::exists(AppPaths::settingsPath())) return;

    QSettings portable(AppPaths::settingsPath(), QSettings::IniFormat);
    const QList<QPair<QString, QString>> legacyApplications{
        {QStringLiteral("LudoShelf"), QStringLiteral("LudoShelf")},
        {QStringLiteral("LudoShelf"), QStringLiteral("LudoShelfApp")},
    };
    for (const auto& [organization, application] : legacyApplications) {
        QSettings legacy(organization, application);
        for (const QString& key : legacy.allKeys()) {
            if (!portable.contains(key)) portable.setValue(key, legacy.value(key));
        }
    }
    portable.sync();
}

} // namespace

QString AppPaths::dataRoot() {
    const QString root = QDir(QCoreApplication::applicationDirPath()).filePath("ludoshelf_data");
    ensureDirectory(root);
    return root;
}

QString AppPaths::databasePath() {
    return QDir(dataRoot()).filePath("ludoshelf.db");
}

QString AppPaths::mediaRoot() {
    const QString path = QDir(dataRoot()).filePath("media");
    ensureDirectory(path);
    return path;
}

QString AppPaths::cacheRoot() {
    const QString path = QDir(dataRoot()).filePath("cache");
    ensureDirectory(path);
    return path;
}

QString AppPaths::backupsRoot() {
    const QString path = QDir(dataRoot()).filePath("backups");
    ensureDirectory(path);
    return path;
}

QString AppPaths::settingsPath() {
    return QDir(dataRoot()).filePath("settings.ini");
}

QString AppPaths::controllerIconsRoot() {
    return QDir(QCoreApplication::applicationDirPath()).filePath("controller_icons");
}

QString AppPaths::legacyDataRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

bool AppPaths::migrateLegacyDataIfNeeded() {
    const QString portableRoot = dataRoot();
    if (!QFileInfo(portableRoot).isWritable()) {
        qCritical() << "Portable data directory is not writable:" << portableRoot;
        return false;
    }

    const QString portableDatabase = databasePath();
    const QString legacyRoot = legacyDataRoot();
    const QString legacyDatabase = QDir(legacyRoot).filePath("ludoshelf.db");
    if (!QFileInfo::exists(portableDatabase) && QFileInfo::exists(legacyDatabase)) {
        if (!copyDirectoryContents(legacyRoot, portableRoot) || !copyDatabaseSnapshot(legacyDatabase, portableDatabase)) {
            QFile::remove(portableDatabase);
            qCritical() << "Could not import the existing library into" << portableRoot;
            return false;
        }
        qInfo() << "Imported existing LudoShelf data into portable directory:" << portableRoot;
    }
    copyLegacySettingsIfNeeded();
    return true;
}

} // namespace LudoShelf::App
