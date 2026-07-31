#ifndef LUDOSHELF_COVERS_RETROARCHCOVERDISCOVERY_H
#define LUDOSHELF_COVERS_RETROARCHCOVERDISCOVERY_H

#include "CoverTypes.h"

#include <QHash>

#include "../domain/Game.h"
#include "../domain/GameFile.h"
#include "../domain/System.h"

namespace LudoShelf::Covers {

struct RetroArchPlaylistMatch {
    QString label;
    QString playlistName;
    QString collectionName;
};

// Built once for an enrichment pass so a large RetroArch playlist directory
// is not reopened and reparsed for every imported ROM.
struct RetroArchDiscoveryContext {
    QStringList configRoots;
    QStringList thumbnailRoots;
    QHash<QString, RetroArchPlaylistMatch> playlistsByGamePath;
};

class RetroArchCoverDiscovery {
public:
    static QStringList configurationRoots();
    static QStringList thumbnailRoots(const QStringList& configRoots = {});
    static RetroArchDiscoveryContext buildDiscoveryContext(const QStringList& configRoots = {});
    static RetroArchPlaylistMatch findPlaylistMatch(const Domain::GameFile& file,
                                                    const QStringList& configRoots = {},
                                                    const RetroArchDiscoveryContext *context = nullptr);
    static QList<CoverCandidate> findLocalCandidates(const Domain::Game& game, const Domain::GameFile& file,
                                                     const Domain::System& system,
                                                     RetroArchPlaylistMatch *playlistMatch = nullptr,
                                                     const QStringList& configRoots = {},
                                                     const QStringList& thumbnails = {},
                                                     const RetroArchDiscoveryContext *context = nullptr);
};

} // namespace LudoShelf::Covers

#endif // LUDOSHELF_COVERS_RETROARCHCOVERDISCOVERY_H
