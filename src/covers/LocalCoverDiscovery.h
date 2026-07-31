#ifndef LUDOSHELF_COVERS_LOCALCOVERDISCOVERY_H
#define LUDOSHELF_COVERS_LOCALCOVERDISCOVERY_H

#include "CoverTypes.h"

#include <QString>
#include <QStringList>

#include "../domain/Game.h"
#include "../domain/GameFile.h"
#include "../domain/System.h"

namespace LudoShelf::Covers {

class LocalCoverDiscovery {
public:
    static QList<CoverCandidate> findCandidates(const Domain::Game& game,
                                                const Domain::GameFile& gameFile,
                                                const Domain::System& system);
    static QString resolveSidecarPath(const QString& sidecarDirectory, const QString& declaredPath,
                                      bool allowExternalPaths = false);

private:
    static void appendImageCandidate(QList<CoverCandidate>& candidates, const QString& path,
                                     const QString& providerId, double priority, CoverKind kind,
                                     const QString& method, const QString& title);
};

} // namespace LudoShelf::Covers

#endif // LUDOSHELF_COVERS_LOCALCOVERDISCOVERY_H
