#ifndef LUDOSHELF_COVERS_LIBRETROCOVERPROVIDER_H
#define LUDOSHELF_COVERS_LIBRETROCOVERPROVIDER_H

#include "CoverTypes.h"
#include "../domain/Game.h"
#include "../domain/System.h"

namespace LudoShelf::Covers {

class LibretroCoverProvider {
public:
    static constexpr const char *Id = "libretro-thumbnails";
    static QString collectionForSystem(const Domain::System& system);
    static QString collectionForMetadataPlatform(const QString& platform);
    static QList<CoverCandidate> candidatesFor(const Domain::Game& game, const Domain::System& system,
                                               const QStringList& titleCandidates = {},
                                               const QString& collectionName = {});
    static QList<CoverCandidate> candidatesForCatalogAssets(const Domain::Game& game, const Domain::System& system,
                                                            const QString& collectionName,
                                                            const QStringList& assetPaths);
    static QString encodePathSegment(const QString& value);
};

} // namespace LudoShelf::Covers

#endif // LUDOSHELF_COVERS_LIBRETROCOVERPROVIDER_H
