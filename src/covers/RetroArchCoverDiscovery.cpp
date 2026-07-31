#include "RetroArchCoverDiscovery.h"

#include "CoverTitleNormalizer.h"
#include "LibretroCoverProvider.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>

namespace LudoShelf::Covers {
namespace {
QStringList effectiveConfigRoots(const QStringList& supplied) {
    if (!supplied.isEmpty()) return supplied;
    const QString home = QDir::homePath();
    return {home + "/.config/retroarch", home + "/.var/app/org.libretro.RetroArch/config/retroarch"};
}

QString normalizedPath(const QString& path) {
    const QFileInfo info(path);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

bool validImage(const QString& path) {
    QImageReader reader(path);
    const QSize size = reader.size();
    return reader.canRead() && size.width() >= 100 && size.height() >= 100 &&
           size.width() <= 16384 && size.height() <= 16384 && qint64(size.width()) * size.height() <= 100000000;
}

QString noPlaylistExtension(QString name) {
    if (name.endsWith(".lpl", Qt::CaseInsensitive)) name.chop(4);
    return name;
}
}

QStringList RetroArchCoverDiscovery::configurationRoots() {
    return effectiveConfigRoots({});
}

QStringList RetroArchCoverDiscovery::thumbnailRoots(const QStringList& configRoots) {
    QStringList roots;
    for (const QString& configRoot : effectiveConfigRoots(configRoots)) {
        QFile config(QDir(configRoot).filePath("retroarch.cfg"));
        if (config.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString content = QString::fromUtf8(config.readAll());
            const QRegularExpression setting(R"cfg(^\s*thumbnails_directory\s*=\s*"([^"]+)"\s*$)cfg", QRegularExpression::MultilineOption);
            const auto match = setting.match(content);
            if (match.hasMatch()) roots.append(match.captured(1));
        }
        roots.append(QDir(configRoot).filePath("thumbnails"));
    }
    roots.removeDuplicates();
    return roots;
}

RetroArchDiscoveryContext RetroArchCoverDiscovery::buildDiscoveryContext(const QStringList& configRoots) {
    RetroArchDiscoveryContext context;
    context.configRoots = effectiveConfigRoots(configRoots);
    context.thumbnailRoots = thumbnailRoots(context.configRoots);
    for (const QString& configRoot : context.configRoots) {
        QDir playlistDir(QDir(configRoot).filePath("playlists"));
        for (const QFileInfo& playlist : playlistDir.entryInfoList({"*.lpl"}, QDir::Files)) {
            QFile source(playlist.absoluteFilePath());
            if (!source.open(QIODevice::ReadOnly)) continue;
            const auto document = QJsonDocument::fromJson(source.readAll());
            if (!document.isObject()) continue;
            for (const auto& value : document.object().value("items").toArray()) {
                const auto item = value.toObject();
                const QString gamePath = normalizedPath(item.value("path").toString());
                if (gamePath.isEmpty() || context.playlistsByGamePath.contains(gamePath)) continue;
                RetroArchPlaylistMatch match;
                match.label = item.value("label").toString();
                match.playlistName = playlist.fileName();
                match.collectionName = noPlaylistExtension(item.value("db_name").toString());
                if (match.collectionName.isEmpty() || match.collectionName == "DETECT")
                    match.collectionName = noPlaylistExtension(match.playlistName);
                context.playlistsByGamePath.insert(gamePath, match);
            }
        }
    }
    return context;
}

RetroArchPlaylistMatch RetroArchCoverDiscovery::findPlaylistMatch(const Domain::GameFile& file,
                                                                   const QStringList& configRoots,
                                                                   const RetroArchDiscoveryContext *context) {
    const QString gamePath = normalizedPath(file.path);
    if (gamePath.isEmpty()) return {};
    if (context) return context->playlistsByGamePath.value(gamePath);
    for (const QString& configRoot : effectiveConfigRoots(configRoots)) {
        QDir playlistDir(QDir(configRoot).filePath("playlists"));
        for (const QFileInfo& playlist : playlistDir.entryInfoList({"*.lpl"}, QDir::Files)) {
            QFile source(playlist.absoluteFilePath());
            if (!source.open(QIODevice::ReadOnly)) continue;
            const auto document = QJsonDocument::fromJson(source.readAll());
            if (!document.isObject()) continue;
            for (const auto& value : document.object().value("items").toArray()) {
                const auto item = value.toObject();
                if (normalizedPath(item.value("path").toString()) != gamePath) continue;
                RetroArchPlaylistMatch match;
                match.label = item.value("label").toString();
                match.playlistName = playlist.fileName();
                match.collectionName = noPlaylistExtension(item.value("db_name").toString());
                if (match.collectionName.isEmpty() || match.collectionName == "DETECT") match.collectionName = noPlaylistExtension(match.playlistName);
                return match;
            }
        }
    }
    return {};
}

QList<CoverCandidate> RetroArchCoverDiscovery::findLocalCandidates(const Domain::Game& game, const Domain::GameFile& file,
                                                                    const Domain::System& system,
                                                                    RetroArchPlaylistMatch *playlistMatch,
                                                                    const QStringList& configRoots,
                                                                    const QStringList& thumbnails,
                                                                    const RetroArchDiscoveryContext *context) {
    const RetroArchPlaylistMatch match = findPlaylistMatch(file, configRoots, context);
    if (playlistMatch) *playlistMatch = match;
    QString collection = match.collectionName;
    if (collection.isEmpty()) {
        const auto onlineCandidates = LibretroCoverProvider::candidatesFor(game, system);
        if (!onlineCandidates.isEmpty()) {
            collection = QUrl::fromPercentEncoding(onlineCandidates.first().downloadUrl.path().section('/', 1, 1).toUtf8());
        }
    }
    if (collection.isEmpty()) return {};

    const QFileInfo fileInfo(file.path);
    const auto normalizedTitle = CoverTitleNormalizer::normalize(game.title);
    const QStringList titles{fileInfo.completeBaseName(), match.label, game.title, normalizedTitle.shortTitle};
    QList<CoverCandidate> results;
    QSet<QString> seen;
    const QStringList effectiveThumbnails = !thumbnails.isEmpty() ? thumbnails
        : context ? context->thumbnailRoots : thumbnailRoots(configRoots);
    for (const QString& root : effectiveThumbnails) {
        for (int index = 0; index < titles.size(); ++index) {
            const QString title = CoverTitleNormalizer::libretroSanitize(titles.at(index));
            if (title.isEmpty()) continue;
            const QString path = QDir(root).filePath(collection + "/Named_Boxarts/" + title + ".png");
            if (seen.contains(path) || !validImage(path)) continue;
            seen.insert(path);
            CoverCandidate candidate;
            candidate.providerId = "retroarch-cache";
            candidate.downloadUrl = QUrl::fromLocalFile(path);
            candidate.sourcePage = candidate.downloadUrl;
            candidate.kind = CoverKind::BoxFront;
            candidate.scope = CoverScope::PlatformRelease;
            candidate.platformId = system.shortName;
            candidate.region = game.region;
            candidate.providerTitle = titles.at(index);
            candidate.matchedLocalTitle = game.title;
            candidate.matchMethod = index == 0 ? "retroarch_rom_filename" : index == 1 ? "retroarch_playlist_label" : index == 2 ? "retroarch_game_title" : "retroarch_short_title_review";
            candidate.matchConfidence = index == 0 ? 1.0 : index == 1 ? 0.98 : index == 2 ? 0.9 : 0.65;
            candidate.sourcePriority = 900;
            candidate.rightsStatus = "local_cache_unknown";
            results.append(candidate);
        }
    }
    return results;
}

} // namespace LudoShelf::Covers
