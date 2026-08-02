#ifndef LUDOSHELF_METADATA_LIBRETRODATABASEPROVIDER_H
#define LUDOSHELF_METADATA_LIBRETRODATABASEPROVIDER_H

#include "RomMetadataProvider.h"

namespace LudoShelf::Metadata {

class LibretroDatabaseProvider final : public RomMetadataProvider {
    Q_OBJECT
public:
    explicit LibretroDatabaseProvider(QObject* parent = nullptr);

    QString id() const override { return QStringLiteral("libretro-database-1.22.1"); }
    QUuid lookupByHashes(const QList<HashCandidate>& candidates, const RomLookupContext& context) override;
    QUuid lookupByTitle(const RomLookupContext& context);

    bool isAvailable() const;
    static bool isDatabaseAvailable();
    static QString databaseRoot();
    static ProviderLookupResult lookupInDirectory(const QString& rdbDirectory,
                                                  const QList<HashCandidate>& candidates,
                                                  const RomLookupContext& context);
    static ProviderLookupResult lookupByTitleInDirectory(const QString& rdbDirectory,
                                                         const RomLookupContext& context);
};

} // namespace LudoShelf::Metadata

#endif // LUDOSHELF_METADATA_LIBRETRODATABASEPROVIDER_H
