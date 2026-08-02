#ifndef LUDOSHELF_MEDIA_MEDIASTORAGEMANAGER_H
#define LUDOSHELF_MEDIA_MEDIASTORAGEMANAGER_H

#include <QString>
#include <QImage>
#include <QUuid>

#include "../database/DatabaseManager.h"
#include "../covers/CoverTypes.h"

namespace LudoShelf::Media {

struct MediaMaintenanceReport {
    int referencedObjects{0};
    int missingObjects{0};
    int orphanedObjects{0};
    int removedObjects{0};
    QStringList missingHashes;
};

class MediaStorageManager {
public:
    static MediaStorageManager& instance();

    QString storeOriginalImage(
        const QUuid& gameId,
        const QByteArray& imageData,
        const QString& mediaType, // "box-front", "screenshot", "logo", "background"
        const QString& = "image/png",
        const QString& source = "local"
    );
    QString storeCoverCandidate(const QUuid& gameId, const QByteArray& imageData, const Covers::CoverCandidate& candidate);

    QImage loadThumbnail(const QString& sha256, int width = 300, int height = 450);
    QString getObjectAbsolutePath(const QString& sha256);
    QString getObjectAbsolutePath(const QString& sha256, const QString& extension);
    // Inspection is non-destructive unless removeOrphans is explicitly true.
    MediaMaintenanceReport auditStorage(bool removeOrphans = false);

private:
    MediaStorageManager() = default;
    QString baseObjectsDirectory() const;
    QString baseCacheDirectory() const;
    QString objectRelativePath(const QString& sha256, const QString& extension) const;
};

} // namespace LudoShelf::Media

#endif // LUDOSHELF_MEDIA_MEDIASTORAGEMANAGER_H
