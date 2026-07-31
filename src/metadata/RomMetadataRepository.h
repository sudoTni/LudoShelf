#ifndef LUDOSHELF_METADATA_ROMMETADATAREPOSITORY_H
#define LUDOSHELF_METADATA_ROMMETADATAREPOSITORY_H

#include "RomMetadataTypes.h"

#include <QUuid>

namespace LudoShelf::Metadata {

struct CachedMetadata {
    bool exists{false};
    ProviderLookupResult result;
    QDateTime fetchedAt;
    QDateTime expiresAt;
    QDateTime negativeUntil;
    bool fresh() const;
};

class RomMetadataRepository {
public:
    static constexpr int PositiveCacheDays = 365;
    static constexpr int NegativeCacheDays = 30;

    CachedMetadata cached(const QUuid& gameId, const QString& provider = QStringLiteral("libretro-database-1.22.1")) const;
    bool saveHashes(const QUuid& gameId, const QList<HashCandidate>& candidates, const QDateTime& sourceModifiedAt) const;
    QList<HashCandidate> hashes(const QUuid& gameId, const QDateTime& sourceModifiedAt) const;
    bool saveResult(const QUuid& gameId, const ProviderLookupResult& result) const;
};

} // namespace LudoShelf::Metadata

#endif // LUDOSHELF_METADATA_ROMMETADATAREPOSITORY_H
