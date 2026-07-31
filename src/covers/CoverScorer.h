#ifndef LUDOSHELF_COVERS_COVERSCORER_H
#define LUDOSHELF_COVERS_COVERSCORER_H

#include "CoverTypes.h"

namespace LudoShelf::Covers {

class CoverScorer {
public:
    static double resolutionScore(int width, int height);
    static double score(CoverCandidate& candidate, const QString& preferredRegion,
                        bool allowFanArt, bool allowStoreCapsules);
};

} // namespace LudoShelf::Covers

#endif // LUDOSHELF_COVERS_COVERSCORER_H
