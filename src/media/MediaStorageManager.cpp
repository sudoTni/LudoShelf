#include "MediaStorageManager.h"
#include "../app/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QBuffer>
#include <QDebug>
#include <QImageReader>
#include <QSaveFile>

namespace LudoShelf::Media {

MediaStorageManager& MediaStorageManager::instance() {
    static MediaStorageManager mgr;
    return mgr;
}

QString MediaStorageManager::baseObjectsDirectory() const {
    const QString path = App::AppPaths::mediaRoot() + "/objects";
    QDir().mkpath(path);
    return path;
}

QString MediaStorageManager::baseCacheDirectory() const {
    const QString path = App::AppPaths::cacheRoot() + "/thumbnails";
    QDir().mkpath(path);
    return path;
}

QString MediaStorageManager::getObjectAbsolutePath(const QString& sha256) {
    const auto stored = Database::DatabaseManager::instance().getMediaObject(sha256);
    if (!stored.sha256.isEmpty()) {
        if (QFileInfo(stored.relativePath).isAbsolute()) return stored.relativePath;
        return QDir(App::AppPaths::dataRoot()).filePath(stored.relativePath);
    }
    return getObjectAbsolutePath(sha256, QStringLiteral("png"));
}

QString MediaStorageManager::objectRelativePath(const QString& sha256, const QString& extension) const {
    if (sha256.length() < 4) return {};
    return QString("media/objects/%1/%2/%3.%4")
        .arg(sha256.left(2), sha256.mid(2, 2), sha256, extension);
}

QString MediaStorageManager::getObjectAbsolutePath(const QString& sha256, const QString& extension) {
    if (sha256.length() < 4 || extension.isEmpty()) return QString();
    QString sub1 = sha256.left(2);
    QString sub2 = sha256.mid(2, 2);
    QString dirPath = baseObjectsDirectory() + "/" + sub1 + "/" + sub2;
    QDir().mkpath(dirPath);
    return dirPath + "/" + sha256 + "." + extension;
}

QString MediaStorageManager::storeOriginalImage(
    const QUuid& gameId,
    const QByteArray& imageData,
    const QString& mediaType,
    const QString&,
    const QString& source
) {
    if (imageData.isEmpty()) return QString();

    QBuffer formatProbe;
    formatProbe.setData(imageData);
    formatProbe.open(QIODevice::ReadOnly);
    const QString format = QString::fromLatin1(QImageReader::imageFormat(&formatProbe)).toLower();

    QBuffer buffer;
    buffer.setData(imageData);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize size = reader.size();
    if (!reader.canRead() || !size.isValid() || size.width() < 100 || size.height() < 100 ||
        size.width() > 16384 || size.height() > 16384 || qint64(size.width()) * size.height() > 100000000) {
        qWarning() << "Failed to decode image data.";
        return QString();
    }

    QImage image = reader.read();
    if (image.isNull()) return {};
    const QHash<QString, QString> extensions{
        {"png", "png"}, {"jpeg", "jpg"}, {"jpg", "jpg"}, {"webp", "webp"}, {"bmp", "bmp"}, {"avif", "avif"}
    };
    const QString extension = extensions.value(format);
    if (extension.isEmpty()) {
        qWarning() << "Unsupported cover image format:" << format;
        return {};
    }
    const QString mimeType = extension == "jpg" ? "image/jpeg" : "image/" + extension;

    QString sha256 = QString::fromUtf8(QCryptographicHash::hash(imageData, QCryptographicHash::Sha256).toHex()).toLower();
    QString targetPath = getObjectAbsolutePath(sha256, extension);

    if (!QFile::exists(targetPath)) {
        QSaveFile file(targetPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(imageData) != imageData.size() || !file.commit()) return {};
    }

    Covers::MediaObject object;
    object.sha256 = sha256;
    object.relativePath = objectRelativePath(sha256, extension);
    object.mimeType = mimeType;
    object.extension = extension;
    object.byteSize = imageData.size();
    object.width = image.width();
    object.height = image.height();
    object.validationState = "validated";
    object.createdAt = QDateTime::currentDateTimeUtc();
    object.validatedAt = object.createdAt;
    if (!Database::DatabaseManager::instance().saveMediaObject(object)) return {};

    // The original game_media relation remains populated for existing installs,
    // while cover_assets carries the provenance and selection contract required
    // by the credential-free acquisition pipeline.
    if (!Database::DatabaseManager::instance().getGame(gameId).id.isNull()) {
        const QString providerId = source == "procedural" ? QStringLiteral("ludoshelf")
                                 : source == "local" ? QStringLiteral("local-user") : source;
        Covers::CoverProvider provider;
        provider.id = providerId;
        provider.displayName = providerId == "ludoshelf" ? QStringLiteral("LudoShelf") : providerId;
        provider.adapterVersion = QStringLiteral("1.0");
        provider.stability = QStringLiteral("local");
        provider.priority = source == "procedural" ? 100 : 1000;
        Database::DatabaseManager::instance().saveCoverProvider(provider);

        Covers::CoverAsset asset;
        asset.gameId = gameId;
        asset.mediaObjectSha256 = sha256;
        asset.providerId = providerId;
        asset.kind = mediaType == "placeholder" ? Covers::CoverKind::GeneratedPlaceholder : Covers::CoverKind::BoxFront;
        asset.scope = Covers::CoverScope::UserGameRecord;
        asset.providerTitle = source;
        asset.matchMethod = source == "procedural" ? QStringLiteral("generated") : QStringLiteral("local_import");
        asset.matchConfidence = 1.0;
        asset.rightsStatus = source == "procedural" ? QStringLiteral("generated_by_application") : QStringLiteral("user_supplied");
        asset.redistributionAllowed = source == "procedural";
        asset.preferred = true;
        asset.userSupplied = source != "procedural";
        asset.userSelected = source == "local-user";
        asset.locked = source == "local-user";
        asset.downloadedAt = QDateTime::currentDateTimeUtc();
        Database::DatabaseManager::instance().saveCoverAsset(asset);
    }

    Database::GameMedia media;
    media.gameId = gameId;
    media.mediaType = mediaType;
    media.path = targetPath;
    media.source = source;
    media.width = image.width();
    media.height = image.height();
    media.preferred = true;
    Database::DatabaseManager::instance().saveGameMedia(media);

    return sha256;
}

QString MediaStorageManager::storeCoverCandidate(const QUuid& gameId, const QByteArray& imageData,
                                                  const Covers::CoverCandidate& candidate) {
    if (candidate.providerId.isEmpty()) return {};
    const QString sha256 = storeOriginalImage(gameId, imageData, Covers::coverKindToString(candidate.kind),
                                               candidate.declaredMimeType, candidate.providerId);
    if (sha256.isEmpty()) return {};

    Covers::CoverProvider provider;
    provider.id = candidate.providerId;
    provider.displayName = candidate.providerId;
    provider.adapterVersion = QStringLiteral("1.0");
    provider.stability = candidate.providerId == "libretro-thumbnails" ? QStringLiteral("public-supported") : QStringLiteral("local");
    provider.priority = static_cast<int>(candidate.sourcePriority);
    if (!Database::DatabaseManager::instance().saveCoverProvider(provider)) return {};

    Covers::CoverAsset asset;
    asset.gameId = gameId;
    asset.mediaObjectSha256 = sha256;
    asset.providerId = candidate.providerId;
    asset.providerAssetId = candidate.providerAssetId;
    asset.providerGameId = candidate.externalGameId;
    asset.kind = candidate.kind;
    asset.scope = candidate.scope;
    asset.platformId = candidate.platformId;
    asset.region = candidate.region;
    asset.languages = candidate.languages;
    asset.edition = candidate.edition;
    asset.sourcePage = candidate.sourcePage;
    asset.sourceUrl = candidate.downloadUrl;
    asset.providerTitle = candidate.providerTitle;
    asset.matchMethod = candidate.matchMethod;
    asset.matchConfidence = candidate.matchConfidence;
    asset.qualityScore = candidate.qualityScore;
    asset.finalScore = candidate.finalScore;
    asset.rightsStatus = candidate.rightsStatus;
    asset.licenseId = candidate.licenseId;
    asset.licenseUrl = candidate.licenseUrl;
    asset.creator = candidate.creator;
    asset.attribution = candidate.attribution;
    asset.preferred = true;
    asset.downloadedAt = QDateTime::currentDateTimeUtc();
    return Database::DatabaseManager::instance().saveCoverAsset(asset) ? sha256 : QString();
}

QImage MediaStorageManager::loadThumbnail(const QString& sha256, int width, int height) {
    if (sha256.isEmpty()) return QImage();

    QString cacheSubDir = QString("%1/%2").arg(baseCacheDirectory()).arg(sha256);
    QDir().mkpath(cacheSubDir);
    QString cacheFilePath = QString("%1/%2x%3.png").arg(cacheSubDir).arg(width).arg(height);

    if (QFile::exists(cacheFilePath)) {
        QImage cachedImage;
        if (cachedImage.load(cacheFilePath)) {
            return cachedImage;
        }
    }

    QString originalPath = getObjectAbsolutePath(sha256);
    if (!QFile::exists(originalPath)) return QImage();

    QImage originalImage(originalPath);
    if (originalImage.isNull()) return QImage();

    QImage thumbnail = originalImage.scaled(width, height, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    thumbnail.save(cacheFilePath, "PNG");

    return thumbnail;
}

} // namespace LudoShelf::Media
