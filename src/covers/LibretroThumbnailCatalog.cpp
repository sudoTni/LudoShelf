#include "LibretroThumbnailCatalog.h"

#include "CoverTitleNormalizer.h"
#include "../app/AppPaths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFutureWatcher>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <QtConcurrentRun>

#include <algorithm>

namespace LudoShelf::Covers {
namespace {

constexpr int CatalogCacheDays = 7;

QString repositoryName(const QString& collection) {
    QString result = collection;
    result.replace(' ', '_');
    return result;
}

QString assetStem(const QString& path) {
    QString value = path.section('/', -1);
    if (value.endsWith(".png", Qt::CaseInsensitive)) value.chop(4);
    return value;
}

QString titleKey(QString value) {
    value = CoverTitleNormalizer::libretroSanitize(value).toCaseFolded();
    value.replace(QRegularExpression(QStringLiteral(R"(\s*[\[(][^\])]*[\])])")), QString());
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(" "));
    // ROM sets and thumbnail repositories frequently disagree on whether a
    // sequel number is written as a Roman numeral.  Normalize whole tokens
    // only, so names such as "mix" are never changed accidentally.
    const QList<QPair<QString, QString>> romanNumerals{
        {"xii", "12"}, {"xi", "11"}, {"x", "10"}, {"ix", "9"}, {"viii", "8"},
        {"vii", "7"}, {"vi", "6"}, {"v", "5"}, {"iv", "4"}, {"iii", "3"}, {"ii", "2"}, {"i", "1"}
    };
    for (const auto& [roman, numeric] : romanNumerals)
        value.replace(QRegularExpression(QStringLiteral("\\b%1\\b").arg(roman)), numeric);
    return value.simplified();
}

QString compactTitleKey(const QString& value) {
    QString compact = titleKey(value);
    compact.remove(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")));
    return compact;
}

QString tagText(QString value) {
    value = CoverTitleNormalizer::libretroSanitize(value).toCaseFolded();
    value.remove(QRegularExpression(QStringLiteral(R"(^.*?(?=\())")));
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(" "));
    return value.simplified();
}

struct TitleCandidate {
    QString sanitized;
    QString foldedSanitized;
    QString key;
    QString compactKey;
    QString tags;
};

QList<TitleCandidate> makeTitleCandidates(const QStringList& titles) {
    QList<TitleCandidate> result;
    for (const QString& title : titles) {
        const QString sanitized = CoverTitleNormalizer::libretroSanitize(title);
        if (sanitized.isEmpty()) continue;
        const TitleCandidate candidate{sanitized, sanitized.toCaseFolded(), titleKey(title), compactTitleKey(title), tagText(title)};
        const auto duplicate = std::find_if(result.cbegin(), result.cend(), [&candidate](const TitleCandidate& existing) {
            return existing.foldedSanitized == candidate.foldedSanitized;
        });
        if (duplicate == result.cend()) result.append(candidate);
    }
    return result;
}

QStringList keyPrefixes(const QString& key) {
    QStringList result;
    if (key.isEmpty()) return result;
    result.append(key);
    for (qsizetype pos = key.indexOf(' '); pos >= 0; pos = key.indexOf(' ', pos + 1)) {
        result.append(key.left(pos));
    }
    return result;
}

template <typename Asset>
int candidateScore(const Asset& asset, const QList<TitleCandidate>& titleCandidates) {
    int best = -1;
    for (const TitleCandidate& title : titleCandidates) {
        int score = -1;
        if (title.foldedSanitized == asset.foldedStem) score = 10000;
        else if ((title.key == asset.key || title.compactKey == asset.compactKey) && !title.key.isEmpty()) score = 1000;
        // A long, catalog-specific subtitle can be safely accepted when the
        // complete local title is otherwise an exact prefix.  This covers
        // names such as "Teenage Mutant Ninja Turtles 3" versus the fully
        // titled "... III - The Manhattan Project" without broad fuzzy
        // matching across unrelated games.
        else if (!title.key.isEmpty() &&
                 (asset.key.startsWith(title.key + ' ') || title.key.startsWith(asset.key + ' '))) score = 750;
        else continue;

        for (const QString& token : {QStringLiteral("usa"), QStringLiteral("europe"), QStringLiteral("japan"),
                                     QStringLiteral("world"), QStringLiteral("korea"), QStringLiteral("asia"),
                                     QStringLiteral("rev"), QStringLiteral("en"), QStringLiteral("unl")}) {
            if (title.tags.contains(token) && asset.tags.contains(token)) score += 20;
        }
        if (asset.foldedStem == title.foldedSanitized) score += 100;
        best = qMax(best, score);
    }
    return best;
}

} // namespace

LibretroThumbnailCatalog::LibretroThumbnailCatalog(QObject *parent) : QObject(parent) {
    m_workerPool.setMaxThreadCount(2);
    m_workerPool.setExpiryTimeout(-1);
}

LibretroThumbnailCatalog::CatalogIndexPtr LibretroThumbnailCatalog::buildIndex(const QStringList& assetPaths) {
    auto index = std::make_shared<CatalogIndex>();
    index->assets.reserve(assetPaths.size());
    for (const QString& path : assetPaths) {
        const QString stem = assetStem(path);
        if (stem.isEmpty()) continue;
        const int assetIndex = static_cast<int>(index->assets.size());
        index->assets.append({path, stem, stem.toCaseFolded(), titleKey(stem), compactTitleKey(stem), tagText(stem)});
        const CatalogAsset& asset = index->assets.constLast();
        index->byFoldedStem[asset.foldedStem].append(assetIndex);
        if (!asset.key.isEmpty()) {
            index->byKey[asset.key].append(assetIndex);
            for (const QString& prefix : keyPrefixes(asset.key)) index->byKeyPrefix[prefix].append(assetIndex);
        }
        if (!asset.compactKey.isEmpty()) index->byCompactKey[asset.compactKey].append(assetIndex);
    }
    return index;
}

QString LibretroThumbnailCatalog::cachePathFor(const QString& collection) const {
    const QString directory = QDir(App::AppPaths::cacheRoot()).filePath(QStringLiteral("libretro-thumbnail-catalog"));
    QDir().mkpath(directory);
    return QDir(directory).filePath(repositoryName(collection) + QStringLiteral(".json"));
}

bool LibretroThumbnailCatalog::loadCachedCollection(const QString& collection) {
    if (m_collections.contains(collection)) return true;
    QFile file(cachePathFor(collection));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return false;
    const QJsonObject object = document.object();
    QStringList paths;
    for (const QJsonValue& value : object.value("assets").toArray()) {
        const QString path = value.toString();
        if (path.startsWith("Named_Boxarts/") && path.endsWith(".png", Qt::CaseInsensitive)) paths.append(path);
    }
    if (paths.isEmpty()) return false;
    replaceCollection(collection, paths);
    m_etags.insert(collection, object.value("etag").toString());
    m_fetchedAt.insert(collection, QDateTime::fromString(object.value("fetchedAt").toString(), Qt::ISODate));
    return true;
}

bool LibretroThumbnailCatalog::cacheIsFresh(const QString& collection) const {
    const QDateTime fetched = m_fetchedAt.value(collection);
    return fetched.isValid() && fetched.addDays(CatalogCacheDays) > QDateTime::currentDateTimeUtc();
}

void LibretroThumbnailCatalog::saveCachedCollection(const QString& collection, const QStringList& assetPaths, const QString& etag) const {
    QJsonArray assets;
    for (const QString& path : assetPaths) assets.append(path);
    const QJsonObject object{{"fetchedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}, {"etag", etag}, {"assets", assets}};
    QFile file(cachePathFor(collection));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QStringList LibretroThumbnailCatalog::selectAssetPaths(const QStringList& assetPaths, const QStringList& titles) {
    return selectAssetPaths(*buildIndex(assetPaths), titles);
}

QStringList LibretroThumbnailCatalog::selectAssetPaths(const CatalogIndex& index, const QStringList& titles) {
    struct ScoredPath { QString path; int score; };
    QList<ScoredPath> scored;
    const QList<TitleCandidate> titleCandidates = makeTitleCandidates(titles);
    QSet<int> candidateIndexes;
    for (const TitleCandidate& title : titleCandidates) {
        for (const int assetIndex : index.byFoldedStem.value(title.foldedSanitized)) candidateIndexes.insert(assetIndex);
        if (!title.key.isEmpty()) {
            for (const int assetIndex : index.byKey.value(title.key)) candidateIndexes.insert(assetIndex);
            for (const int assetIndex : index.byKeyPrefix.value(title.key)) candidateIndexes.insert(assetIndex);
            for (const QString& prefix : keyPrefixes(title.key)) {
                for (const int assetIndex : index.byKey.value(prefix)) candidateIndexes.insert(assetIndex);
            }
        }
        if (!title.compactKey.isEmpty()) {
            for (const int assetIndex : index.byCompactKey.value(title.compactKey)) candidateIndexes.insert(assetIndex);
        }
    }
    for (const int assetIndex : candidateIndexes) {
        const CatalogAsset& asset = index.assets.at(assetIndex);
        const int score = candidateScore(asset, titleCandidates);
        if (score >= 0) scored.append({asset.path, score});
    }
    std::sort(scored.begin(), scored.end(), [](const ScoredPath& left, const ScoredPath& right) {
        if (left.score != right.score) return left.score > right.score;
        return left.path.compare(right.path, Qt::CaseInsensitive) < 0;
    });
    QStringList result;
    for (const ScoredPath& path : scored) {
        if (!result.contains(path.path, Qt::CaseInsensitive)) result.append(path.path);
        if (result.size() == 3) break;
    }
    return result;
}

void LibretroThumbnailCatalog::resolve(const QString& collection, const QStringList& titles, ResolveCallback callback) {
    if (collection.isEmpty()) {
        QTimer::singleShot(0, this, [callback = std::move(callback)]() { callback({}); });
        return;
    }
    const bool loaded = loadCachedCollection(collection);
    if (loaded) {
        m_pending[collection].append({titles, std::move(callback)});
        if (m_indexes.contains(collection)) resolvePending(collection);
        else startIndexBuild(collection);
        if (!cacheIsFresh(collection)) fetchCollection(collection);
        return;
    }
    m_pending[collection].append({titles, std::move(callback)});
    fetchCollection(collection);
}

void LibretroThumbnailCatalog::replaceCollection(const QString& collection, const QStringList& assetPaths) {
    m_collections.insert(collection, assetPaths);
    m_indexes.remove(collection);
    m_collectionRevisions[collection] = m_collectionRevisions.value(collection) + 1;
}

void LibretroThumbnailCatalog::startIndexBuild(const QString& collection) {
    if (!m_collections.contains(collection) || m_indexBuilding.contains(collection)) return;
    m_indexBuilding.insert(collection);
    const quint64 revision = m_collectionRevisions.value(collection);
    const QStringList assets = m_collections.value(collection);
    auto *watcher = new QFutureWatcher<CatalogIndexPtr>(this);
    connect(watcher, &QFutureWatcher<CatalogIndexPtr>::finished, this, [this, watcher, collection, revision]() {
        const CatalogIndexPtr index = watcher->result();
        watcher->deleteLater();
        m_indexBuilding.remove(collection);
        if (m_collectionRevisions.value(collection) != revision) {
            startIndexBuild(collection);
            return;
        }
        m_indexes.insert(collection, index);
        resolvePending(collection);
    });
    watcher->setFuture(QtConcurrent::run(&m_workerPool, [assets] { return buildIndex(assets); }));
}

void LibretroThumbnailCatalog::resolveWithIndex(CatalogIndexPtr index, const QStringList& titles, ResolveCallback callback) {
    auto *watcher = new QFutureWatcher<QStringList>(this);
    connect(watcher, &QFutureWatcher<QStringList>::finished, this, [watcher, callback = std::move(callback)]() mutable {
        const QStringList results = watcher->result();
        watcher->deleteLater();
        callback(results);
    });
    watcher->setFuture(QtConcurrent::run(&m_workerPool, [index = std::move(index), titles] {
        return selectAssetPaths(*index, titles);
    }));
}

void LibretroThumbnailCatalog::fetchCollection(const QString& collection) {
    if (m_fetching.contains(collection)) return;
    m_fetching.insert(collection);
    const QUrl url(QStringLiteral("https://api.github.com/repos/libretro-thumbnails/%1/git/trees/master?recursive=1").arg(repositoryName(collection)));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "LudoShelf/0.1 thumbnail-catalog-client");
    request.setRawHeader("Accept", "application/vnd.github+json");
    const QString etag = m_etags.value(collection);
    if (!etag.isEmpty()) request.setRawHeader("If-None-Match", etag.toUtf8());
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, collection]() {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray body = reply->readAll();
        const QString responseEtag = QString::fromUtf8(reply->rawHeader("ETag"));
        reply->deleteLater();
        m_fetching.remove(collection);
        if (status == 200) {
            const QJsonDocument document = QJsonDocument::fromJson(body);
            QStringList paths;
            for (const QJsonValue& value : document.object().value("tree").toArray()) {
                const QString path = value.toObject().value("path").toString();
                if (path.startsWith("Named_Boxarts/") && path.endsWith(".png", Qt::CaseInsensitive)) paths.append(path);
            }
            if (!paths.isEmpty()) {
                replaceCollection(collection, paths);
                m_etags.insert(collection, responseEtag);
                m_fetchedAt.insert(collection, QDateTime::currentDateTimeUtc());
                saveCachedCollection(collection, paths, responseEtag);
                startIndexBuild(collection);
            }
        } else if (status == 304 && m_collections.contains(collection)) {
            m_fetchedAt.insert(collection, QDateTime::currentDateTimeUtc());
            saveCachedCollection(collection, m_collections.value(collection), m_etags.value(collection));
        }
        resolvePending(collection);
    });
}

void LibretroThumbnailCatalog::resolvePending(const QString& collection) {
    const QList<PendingResolution> pending = m_pending.take(collection);
    const CatalogIndexPtr index = m_indexes.value(collection);
    // A catalog is an optimization, not a dependency.  Network failures,
    // GitHub rate limits, and malformed responses must still release every
    // pending cover request so LibretroCoverProvider can use its conservative
    // filename/title fallback.  Previously those callbacks were discarded,
    // which made automatic cover acquisition appear to stop after metadata.
    if (!index) {
        for (const PendingResolution& request : pending)
            request.callback({});
        return;
    }
    for (const PendingResolution& request : pending)
        resolveWithIndex(index, request.titles, request.callback);
}

} // namespace LudoShelf::Covers
