#ifndef LUDOSHELF_COVERS_LIBRETROTHUMBNAILCATALOG_H
#define LUDOSHELF_COVERS_LIBRETROTHUMBNAILCATALOG_H

#include <QObject>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QNetworkAccessManager>
#include <QSet>
#include <QStringList>
#include <QThreadPool>

#include <functional>
#include <memory>

namespace LudoShelf::Covers {

// A small, portable cache of the official thumbnail repository's file list.
// Looking in this catalog before requesting an image prevents a burst of
// guessed URLs for titles whose ROM-set and thumbnail names differ.
class LibretroThumbnailCatalog : public QObject {
    Q_OBJECT
public:
    using ResolveCallback = std::function<void(const QStringList& assetPaths)>;

    explicit LibretroThumbnailCatalog(QObject *parent = nullptr);

    // Resolves actual Named_Boxarts file names for the requested titles.  The
    // callback is always invoked asynchronously, so large cached catalogs
    // never block the GUI event loop while titles are matched.
    void resolve(const QString& collection, const QStringList& titles, ResolveCallback callback);

    static QStringList selectAssetPaths(const QStringList& assetPaths, const QStringList& titles);

private:
    struct CatalogAsset {
        QString path;
        QString stem;
        QString foldedStem;
        QString key;
        QString compactKey;
        QString tags;
    };

    // The GitHub tree for a large collection can contain many thousands of
    // boxarts.  Keep the expensive normalisation work here, once per catalog,
    // rather than repeating it for every ROM being enriched.
    struct CatalogIndex {
        QList<CatalogAsset> assets;
        QHash<QString, QList<int>> byFoldedStem;
        QHash<QString, QList<int>> byKey;
        QHash<QString, QList<int>> byCompactKey;
        QHash<QString, QList<int>> byKeyPrefix;
    };
    using CatalogIndexPtr = std::shared_ptr<const CatalogIndex>;

    struct PendingResolution {
        QStringList titles;
        ResolveCallback callback;
    };

    static CatalogIndexPtr buildIndex(const QStringList& assetPaths);
    static QStringList selectAssetPaths(const CatalogIndex& index, const QStringList& titles);
    QString cachePathFor(const QString& collection) const;
    bool loadCachedCollection(const QString& collection);
    bool cacheIsFresh(const QString& collection) const;
    void saveCachedCollection(const QString& collection, const QStringList& assetPaths, const QString& etag) const;
    void replaceCollection(const QString& collection, const QStringList& assetPaths);
    void startIndexBuild(const QString& collection);
    void resolveWithIndex(CatalogIndexPtr index, const QStringList& titles, ResolveCallback callback);
    void fetchCollection(const QString& collection);
    void resolvePending(const QString& collection);

    QNetworkAccessManager m_network;
    QHash<QString, QStringList> m_collections;
    QHash<QString, CatalogIndexPtr> m_indexes;
    QHash<QString, quint64> m_collectionRevisions;
    QHash<QString, QString> m_etags;
    QHash<QString, QDateTime> m_fetchedAt;
    QHash<QString, QList<PendingResolution>> m_pending;
    QSet<QString> m_fetching;
    QSet<QString> m_indexBuilding;
    // Catalog work must remain available while the global Qt worker pool is
    // busy hashing a newly imported collection.
    QThreadPool m_workerPool;
};

} // namespace LudoShelf::Covers

#endif // LUDOSHELF_COVERS_LIBRETROTHUMBNAILCATALOG_H
