#ifndef LUDOSHELF_METADATA_ROMMETADATAPROVIDER_H
#define LUDOSHELF_METADATA_ROMMETADATAPROVIDER_H

#include "RomMetadataTypes.h"

#include <QObject>
#include <QUuid>

#include <utility>

namespace LudoShelf::Metadata {

struct RomLookupContext {
    QString localPlatform;
    QString libraryTitle;
    bool allowAiDescriptions;
    QString localPlatformShortName;
    QString libraryRegion;

    RomLookupContext(QString platform = {}, QString title = {}, bool allowAi = false, QString shortName = {}, QString region = {})
        : localPlatform(std::move(platform)), libraryTitle(std::move(title)), allowAiDescriptions(allowAi),
          localPlatformShortName(std::move(shortName)), libraryRegion(std::move(region)) {}
};

class RomMetadataProvider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual QString id() const = 0;
    virtual QUuid lookupByHashes(const QList<HashCandidate>& candidates, const RomLookupContext& context) = 0;
signals:
    void lookupFinished(const QUuid& requestId, const ProviderLookupResult& result);
};

} // namespace LudoShelf::Metadata

#endif // LUDOSHELF_METADATA_ROMMETADATAPROVIDER_H
