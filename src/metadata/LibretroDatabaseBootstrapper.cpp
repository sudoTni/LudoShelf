#include "LibretroDatabaseBootstrapper.h"

#include "LibretroDatabaseProvider.h"
#include "../app/AppPaths.h"

#include <archive.h>
#include <archive_entry.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QUrl>
#include <QtConcurrentRun>

#include <algorithm>

namespace LudoShelf::Metadata {
namespace {

constexpr auto DatabaseUrl = "https://github.com/libretro/libretro-database/archive/refs/tags/v1.22.1.zip";
constexpr auto DatabaseSha256 = "f98314970f42efdc4bb0d55606b23bec25b74660d1fe8f623eed1aa549e47d12";
constexpr qint64 MaximumDownloadBytes = 256LL * 1024 * 1024;
constexpr qint64 MaximumExtractedBytes = 512LL * 1024 * 1024;
constexpr int MaximumExtractedFiles = 500;
const QString BundleName = QStringLiteral("libretro-database-1.22.1");
const QString RdbArchivePrefix = BundleName + QStringLiteral("/rdb/");

struct ExtractionResult {
    bool success{false};
    QString message;
};

bool safeRelativePath(const QString& value) {
    if (value.isEmpty() || value.startsWith('/') || value.startsWith('\\')) return false;
    const QStringList parts = value.split('/', Qt::SkipEmptyParts);
    return !parts.isEmpty() && std::none_of(parts.cbegin(), parts.cend(), [](const QString& part) {
        return part == QStringLiteral(".") || part == QStringLiteral("..");
    });
}

ExtractionResult extractRdbBundle(const QString& archivePath, const QString& temporaryRoot, const QString& dataRoot) {
    const QString stagingBundle = QDir(temporaryRoot).filePath(BundleName);
    const QString stagingRdb = QDir(stagingBundle).filePath(QStringLiteral("rdb"));
    if (!QDir().mkpath(stagingRdb)) return {false, QStringLiteral("Could not create a staging directory for the Libretro database.")};

    archive* reader = archive_read_new();
    archive_read_support_filter_all(reader);
    archive_read_support_format_zip(reader);
    if (archive_read_open_filename(reader, archivePath.toUtf8().constData(), 128 * 1024) != ARCHIVE_OK) {
        const QString error = QString::fromUtf8(archive_error_string(reader));
        archive_read_free(reader);
        return {false, QStringLiteral("Could not open the Libretro database archive: %1").arg(error)};
    }

    archive_entry* entry = nullptr;
    int files = 0;
    qint64 extractedBytes = 0;
    bool foundRdb = false;
    QString error;
    int headerStatus = ARCHIVE_OK;
    while ((headerStatus = archive_read_next_header(reader, &entry)) == ARCHIVE_OK) {
        const QString archivedPath = QString::fromUtf8(archive_entry_pathname(entry));
        if (!archivedPath.startsWith(RdbArchivePrefix)) {
            archive_read_data_skip(reader);
            continue;
        }

        const QString relativePath = archivedPath.mid(RdbArchivePrefix.size());
        if (relativePath.isEmpty()) {
            archive_read_data_skip(reader);
            continue;
        }
        if (!safeRelativePath(relativePath)) { error = QStringLiteral("The downloaded archive contains an unsafe path."); break; }
        const auto type = archive_entry_filetype(entry);
        if (type == AE_IFDIR) {
            if (!QDir().mkpath(QDir(stagingRdb).filePath(relativePath))) { error = QStringLiteral("Could not create a database directory."); break; }
            continue;
        }
        if (type != AE_IFREG) { error = QStringLiteral("The downloaded archive contains an unsupported entry type."); break; }

        const qint64 entrySize = archive_entry_size(entry);
        if (entrySize < 0 || ++files > MaximumExtractedFiles || entrySize > MaximumExtractedBytes - extractedBytes) {
            error = QStringLiteral("The downloaded archive exceeds safe extraction limits.");
            break;
        }
        const QString outputPath = QDir(stagingRdb).filePath(relativePath);
        if (!QDir().mkpath(QFileInfo(outputPath).dir().absolutePath())) { error = QStringLiteral("Could not create a database directory."); break; }
        QFile output(outputPath);
        if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) { error = QStringLiteral("Could not write an extracted database file."); break; }
        char buffer[128 * 1024];
        qint64 remaining = entrySize;
        while (remaining > 0) {
            const la_ssize_t read = archive_read_data(reader, buffer, static_cast<size_t>(qMin<qint64>(remaining, sizeof(buffer))));
            if (read <= 0 || output.write(buffer, read) != read) { error = QStringLiteral("Could not extract a database file."); break; }
            remaining -= read;
        }
        output.close();
        if (!error.isEmpty()) break;
        extractedBytes += entrySize;
        foundRdb = foundRdb || relativePath.endsWith(QStringLiteral(".rdb"), Qt::CaseInsensitive);
    }
    if (headerStatus != ARCHIVE_EOF && error.isEmpty())
        error = QStringLiteral("Could not read the downloaded Libretro database archive.");
    archive_read_free(reader);
    if (!error.isEmpty()) return {false, error};
    if (!foundRdb) return {false, QStringLiteral("The downloaded archive did not contain any RDB files.")};

    const QString targetBundle = QDir(dataRoot).filePath(BundleName);
    if (QFileInfo::exists(targetBundle) && !QDir(targetBundle).removeRecursively())
        return {false, QStringLiteral("Could not replace the incomplete Libretro database.")};
    if (!QDir().rename(stagingBundle, targetBundle))
        return {false, QStringLiteral("Could not install the Libretro database.")};
    if (!LibretroDatabaseProvider::isDatabaseAvailable())
        return {false, QStringLiteral("The installed Libretro database is incomplete.")};
    return {true, QStringLiteral("Libretro database 1.22.1 is ready.")};
}

} // namespace

LibretroDatabaseBootstrapper::LibretroDatabaseBootstrapper(QObject* parent)
    : QObject(parent), m_network(new QNetworkAccessManager(this)) {}

void LibretroDatabaseBootstrapper::ensureAvailable() {
    if (m_active) return;
    if (LibretroDatabaseProvider::isDatabaseAvailable()) {
        emit downloadFinished(true, QStringLiteral("Libretro database 1.22.1 is ready."));
        return;
    }

    const QString dataRoot = App::AppPaths::dataRoot();
    m_temporaryDirectory = std::make_unique<QTemporaryDir>(QDir(dataRoot).filePath(QStringLiteral("libretro-download-XXXXXX")));
    if (!m_temporaryDirectory->isValid()) { fail(QStringLiteral("Could not create temporary storage for the Libretro database download.")); return; }
    m_downloadFile = std::make_unique<QFile>(QDir(m_temporaryDirectory->path()).filePath(QStringLiteral("libretro-database.zip")));
    if (!m_downloadFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) { fail(QStringLiteral("Could not create the Libretro database download file.")); return; }
    m_downloadHash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_active = true;
    emit downloadStarted();

    QNetworkRequest request{QUrl(QString::fromLatin1(DatabaseUrl))};
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("LudoShelf/0.2 libretro-database-bootstrapper"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this] {
        const QByteArray bytes = m_reply->readAll();
        if (m_downloadFile->size() + bytes.size() > MaximumDownloadBytes || m_downloadFile->write(bytes) != bytes.size()) {
            m_reply->abort();
        } else {
            m_downloadHash->addData(bytes);
        }
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, dataRoot] {
        const int status = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError networkError = m_reply->error();
        const QString networkMessage = m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        m_downloadFile->close();
        if (networkError != QNetworkReply::NoError || status < 200 || status >= 300) {
            fail(QStringLiteral("Could not download the Libretro database: %1").arg(networkMessage));
            return;
        }
        if (m_downloadFile->size() <= 0 || m_downloadFile->size() > MaximumDownloadBytes) {
            fail(QStringLiteral("The Libretro database download exceeded safe size limits."));
            return;
        }
        const QString actualHash = QString::fromLatin1(m_downloadHash->result().toHex());
        if (QString::fromLatin1(DatabaseSha256).compare(actualHash, Qt::CaseInsensitive) != 0) {
            fail(QStringLiteral("The Libretro database download failed checksum verification."));
            return;
        }
        const QString archivePath = m_downloadFile->fileName();
        m_downloadFile.reset();
        beginExtraction(archivePath, dataRoot);
    });
}

void LibretroDatabaseBootstrapper::beginExtraction(const QString& archivePath, const QString& dataRoot) {
    auto* watcher = new QFutureWatcher<ExtractionResult>(this);
    connect(watcher, &QFutureWatcher<ExtractionResult>::finished, this, [this, watcher] {
        const ExtractionResult result = watcher->result();
        watcher->deleteLater();
        m_temporaryDirectory.reset();
        m_downloadHash.reset();
        m_active = false;
        emit downloadFinished(result.success, result.message);
    });
    const QString temporaryRoot = m_temporaryDirectory->path();
    watcher->setFuture(QtConcurrent::run([archivePath, temporaryRoot, dataRoot] {
        return extractRdbBundle(archivePath, temporaryRoot, dataRoot);
    }));
}

void LibretroDatabaseBootstrapper::fail(const QString& message) {
    if (m_downloadFile) m_downloadFile->close();
    m_downloadFile.reset();
    m_downloadHash.reset();
    m_temporaryDirectory.reset();
    m_active = false;
    emit downloadFinished(false, message);
}

} // namespace LudoShelf::Metadata
