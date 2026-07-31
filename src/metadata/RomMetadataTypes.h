#ifndef LUDOSHELF_METADATA_ROMMETADATATYPES_H
#define LUDOSHELF_METADATA_ROMMETADATATYPES_H

#include <QDateTime>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace LudoShelf::Metadata {

struct HashCandidate {
    QString payloadKey;
    qint64 byteSize{0};
    QString crc32;
    QString md5;
    QString sha1;
    QString sha256;
};

struct FieldProvenance {
    QString source;
    QString confidence;
};

struct RomMetadata {
    static constexpr int NormalizationVersion = 4;
    QString romId;
    int normalizationVersion{NormalizationVersion};
    QString canonicalTitle;
    QString platform;
    int releaseYear{0};
    QString publisher;
    QString developer;
    QStringList genres;
    QString playerCount;
    QString description;
    bool descriptionIsAiGenerated{false};
    QStringList regions;
    QStringList languages;
    QString revision;
    QString mediaType;
    QString developmentStatus;
    QString dumpSource;
    QString provider{QStringLiteral("libretro-database-1.22.1")};
    QString providerRecordId;
    QString identityConfidence{QStringLiteral("unverified")};
    QString metadataConfidence{QStringLiteral("none")};
    QString matchedHashAlgorithm;
    QString matchedHash;
    QMap<QString, FieldProvenance> fieldProvenance;
    QDateTime fetchedAt;
    QDateTime expiresAt;
};

enum class MetadataResultKind { Match, NoMatch, TemporaryError, PermanentError, Unsupported };

struct ProviderLookupResult {
    MetadataResultKind kind{MetadataResultKind::PermanentError};
    RomMetadata metadata;
    QString rawResponse;
    QString message;
    QDateTime retryAfter;
};

QString normalizeHash(const QString& value, int expectedLength);
QJsonObject toJson(const RomMetadata& metadata);
RomMetadata fromJson(const QJsonObject& json);
QString resultKindToString(MetadataResultKind kind);
MetadataResultKind resultKindFromString(const QString& kind);

} // namespace LudoShelf::Metadata

#endif // LUDOSHELF_METADATA_ROMMETADATATYPES_H
