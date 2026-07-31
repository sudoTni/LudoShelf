#include "LocalCoverDiscovery.h"
#include "CoverTitleNormalizer.h"
#include "CoverScorer.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace LudoShelf::Covers {
namespace {
const QStringList kExtensions{"png", "jpg", "jpeg", "webp", "bmp", "avif"};
const QStringList kDirectories{"Box Front", "BoxFront", "box-front", "boxart", "boxarts", "covers", "cover", "front", "media", "images", "artwork"};

bool isSafeImage(const QString& path) {
    QImageReader reader(path);
    const QSize size = reader.size();
    return reader.canRead() && size.width() >= 100 && size.height() >= 100 &&
           size.width() <= 16384 && size.height() <= 16384 && qint64(size.width()) * size.height() <= 100000000;
}
}

QString LocalCoverDiscovery::resolveSidecarPath(const QString& sidecarDirectory, const QString& declaredPath,
                                                bool allowExternalPaths) {
    const QUrl declaredUrl(declaredPath);
    if (declaredPath.isEmpty() || (declaredUrl.isValid() && !declaredUrl.scheme().isEmpty())) return {};
    const QFileInfo base(sidecarDirectory);
    const QFileInfo candidate(QDir(sidecarDirectory), declaredPath);
    const QString canonicalBase = base.canonicalFilePath();
    const QString canonicalCandidate = candidate.canonicalFilePath();
    if (canonicalCandidate.isEmpty()) return {};
    if (!allowExternalPaths && (canonicalBase.isEmpty() ||
        (canonicalCandidate != canonicalBase && !canonicalCandidate.startsWith(canonicalBase + '/')))) return {};
    return canonicalCandidate;
}

void LocalCoverDiscovery::appendImageCandidate(QList<CoverCandidate>& candidates, const QString& path,
                                               const QString& providerId, double priority, CoverKind kind,
                                               const QString& method, const QString& title) {
    if (!QFileInfo::exists(path) || !isSafeImage(path)) return;
    const QSize size = QImageReader(path).size();
    CoverCandidate candidate;
    candidate.providerId = providerId;
    candidate.downloadUrl = QUrl::fromLocalFile(path);
    candidate.sourcePage = candidate.downloadUrl;
    candidate.kind = kind;
    candidate.scope = CoverScope::UserGameRecord;
    candidate.providerTitle = title;
    candidate.matchedLocalTitle = title;
    candidate.matchMethod = method;
    candidate.matchConfidence = 1.0;
    candidate.declaredWidth = size.width();
    candidate.declaredHeight = size.height();
    candidate.sourcePriority = priority;
    candidate.rightsStatus = providerId == "local-sidecar" ? "user_supplied" : "local_cache_unknown";
    CoverScorer::score(candidate, {}, true, true);
    candidates.append(candidate);
}

QList<CoverCandidate> LocalCoverDiscovery::findCandidates(const Domain::Game& game,
                                                           const Domain::GameFile& gameFile,
                                                           const Domain::System&) {
    QList<CoverCandidate> candidates;
    const QFileInfo gameInfo(gameFile.path);
    if (!gameInfo.exists()) return candidates;
    const QDir directory = gameInfo.dir();
    const QString exactStem = gameInfo.completeBaseName();
    const auto normalized = CoverTitleNormalizer::normalize(exactStem);
    const QStringList stems{exactStem, normalized.canonicalTitle, normalized.shortTitle};
    QSet<QString> seen;

    const QStringList sidecars{"ludoshelf.json", "game.json", "metadata.json", "media.json"};
    for (const QString& sidecar : sidecars) {
        QFile file(directory.filePath(sidecar));
        if (!file.open(QIODevice::ReadOnly)) continue;
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) continue;
        for (const auto& value : document.object().value("covers").toArray()) {
            const auto cover = value.toObject();
            const QString path = resolveSidecarPath(directory.absolutePath(), cover.value("path").toString());
            if (path.isEmpty() || seen.contains(path)) continue;
            seen.insert(path);
            const QString kind = cover.value("kind").toString();
            appendImageCandidate(candidates, path, "local-sidecar", 950,
                kind == "box_front" ? CoverKind::BoxFront : CoverKind::Unknown,
                "explicit_sidecar_link", game.title);
        }
    }

    QStringList searchDirectories{directory.absolutePath()};
    for (const QString& name : kDirectories) if (directory.exists(name)) searchDirectories.append(directory.filePath(name));
    for (const QString& searchDirectory : searchDirectories) {
        for (const QString& stem : stems) {
            if (stem.isEmpty()) continue;
            for (const QString& suffix : QStringList{"", "-front", "-cover", "-boxfront"}) {
                for (const QString& extension : kExtensions) {
                    const QString path = QDir(searchDirectory).filePath(stem + suffix + '.' + extension);
                    if (seen.contains(path)) continue;
                    seen.insert(path);
                    appendImageCandidate(candidates, path, "local-adjacent", 925, CoverKind::BoxFront,
                                         suffix.isEmpty() ? "exact_filename_stem" : "named_adjacent_cover", game.title);
                }
            }
        }
    }
    return candidates;
}

} // namespace LudoShelf::Covers
