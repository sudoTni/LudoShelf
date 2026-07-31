#include "CoverScorer.h"

namespace LudoShelf::Covers {

double CoverScorer::resolutionScore(int width, int height) {
    const qint64 pixels = qint64(width) * height;
    if (pixels >= 4000000) return 40;
    if (pixels >= 2000000) return 35;
    if (pixels >= 1000000) return 30;
    if (pixels >= 500000) return 22;
    if (pixels >= 200000) return 12;
    return 0;
}

double CoverScorer::score(CoverCandidate& candidate, const QString& preferredRegion,
                          bool allowFanArt, bool allowStoreCapsules) {
    if ((!allowFanArt && candidate.kind == CoverKind::FanCreatedCover) ||
        (!allowStoreCapsules && (candidate.kind == CoverKind::StoreVerticalCapsule || candidate.kind == CoverKind::StoreHorizontalCapsule))) {
        candidate.finalScore = -1;
        return candidate.finalScore;
    }
    double value = candidate.sourcePriority + candidate.matchConfidence * 100.0;
    value += resolutionScore(candidate.declaredWidth, candidate.declaredHeight);
    if (!preferredRegion.isEmpty() && candidate.region.compare(preferredRegion, Qt::CaseInsensitive) == 0) value += 80;
    if (candidate.kind == CoverKind::BoxFront || candidate.kind == CoverKind::JewelCaseFront || candidate.kind == CoverKind::ArcadeFlyerFront) value += 80;
    if (candidate.kind == CoverKind::StoreVerticalCapsule || candidate.kind == CoverKind::LibraryVerticalArt) value -= 30;
    if (candidate.kind == CoverKind::FanCreatedCover) value -= 50;
    if (candidate.rightsStatus == "unknown") value -= 10;
    candidate.qualityScore = resolutionScore(candidate.declaredWidth, candidate.declaredHeight);
    candidate.finalScore = value;
    return value;
}

} // namespace LudoShelf::Covers
