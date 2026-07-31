#ifndef LUDOSHELF_METADATA_ROMHASHSERVICE_H
#define LUDOSHELF_METADATA_ROMHASHSERVICE_H

#include "RomMetadataTypes.h"

#include <QDateTime>
#include <atomic>
#include <memory>

namespace LudoShelf::Metadata {

struct RomHashBatch {
    QList<HashCandidate> candidates;
    QString unsupportedReason;
    QString error;
};

class RomHashService {
public:
    // Version 4 hashes only CHD data tracks with SHA-1.  The bundled Libretro
    // RDB uses SHA-1 for these disc records, so this avoids hashing audio and
    // three unused digest algorithms.  Bumping the version lets existing CHD
    // no-match records be retried with the corrected payload set.
    static constexpr int HashSchemaVersion = 4;
    // RVZ/CSO images are transformed disc containers: their stored bytes do
    // not correspond to Libretro's disc hashes.  This also recognizes those
    // images when they are the payload of a ZIP or 7z archive.
    static bool requiresTitleLookup(const QString& path);
    static RomHashBatch discoverAndHash(const QString& path, const QDateTime& modifiedAt,
                                        const std::shared_ptr<std::atomic_bool>& cancelled = {});
    static HashCandidate hashPlainFile(const QString& path, const QString& payloadKey,
                                       const std::shared_ptr<std::atomic_bool>& cancelled = {});
};

} // namespace LudoShelf::Metadata

#endif // LUDOSHELF_METADATA_ROMHASHSERVICE_H
