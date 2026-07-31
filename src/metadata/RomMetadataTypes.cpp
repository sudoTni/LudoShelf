#include "RomMetadataTypes.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace LudoShelf::Metadata {

QString normalizeHash(const QString& value, int expectedLength) {
    QString normalized = value.toUpper();
    normalized.remove(QRegularExpression("[^0-9A-F]"));
    return normalized.size() == expectedLength ? normalized : QString();
}

QJsonObject toJson(const RomMetadata& metadata) {
    QJsonObject provenance;
    for (auto it = metadata.fieldProvenance.cbegin(); it != metadata.fieldProvenance.cend(); ++it)
        provenance.insert(it.key(), QJsonObject{{"source", it->source}, {"confidence", it->confidence}});
    return {{"romId", metadata.romId}, {"normalizationVersion", metadata.normalizationVersion}, {"canonicalTitle", metadata.canonicalTitle}, {"platform", metadata.platform},
            {"releaseYear", metadata.releaseYear}, {"publisher", metadata.publisher}, {"developer", metadata.developer},
            {"genres", QJsonArray::fromStringList(metadata.genres)}, {"playerCount", metadata.playerCount},
            {"description", metadata.description}, {"descriptionIsAiGenerated", metadata.descriptionIsAiGenerated},
            {"regions", QJsonArray::fromStringList(metadata.regions)}, {"languages", QJsonArray::fromStringList(metadata.languages)},
            {"revision", metadata.revision}, {"mediaType", metadata.mediaType}, {"developmentStatus", metadata.developmentStatus}, {"dumpSource", metadata.dumpSource},
            {"provider", metadata.provider}, {"providerRecordId", metadata.providerRecordId},
            {"identityConfidence", metadata.identityConfidence}, {"metadataConfidence", metadata.metadataConfidence},
            {"matchedHashAlgorithm", metadata.matchedHashAlgorithm}, {"matchedHash", metadata.matchedHash}, {"fieldProvenance", provenance},
            {"fetchedAt", metadata.fetchedAt.toString(Qt::ISODate)}, {"expiresAt", metadata.expiresAt.toString(Qt::ISODate)}};
}

RomMetadata fromJson(const QJsonObject& json) {
    RomMetadata metadata;
    metadata.romId = json.value("romId").toString(); metadata.normalizationVersion = json.value("normalizationVersion").toInt(); metadata.canonicalTitle = json.value("canonicalTitle").toString();
    metadata.platform = json.value("platform").toString(); metadata.releaseYear = json.value("releaseYear").toInt();
    metadata.publisher = json.value("publisher").toString(); metadata.developer = json.value("developer").toString();
    for (const auto& value : json.value("genres").toArray()) metadata.genres.append(value.toString());
    metadata.playerCount = json.value("playerCount").toString(); metadata.description = json.value("description").toString();
    metadata.descriptionIsAiGenerated = json.value("descriptionIsAiGenerated").toBool();
    for (const auto& value : json.value("regions").toArray()) metadata.regions.append(value.toString());
    for (const auto& value : json.value("languages").toArray()) metadata.languages.append(value.toString());
    metadata.revision = json.value("revision").toString(); metadata.mediaType = json.value("mediaType").toString(); metadata.developmentStatus = json.value("developmentStatus").toString();
    metadata.dumpSource = json.value("dumpSource").toString(); metadata.provider = json.value("provider").toString("libretro-database-1.22.1");
    metadata.providerRecordId = json.value("providerRecordId").toString();
    metadata.identityConfidence = json.value("identityConfidence").toString("unverified");
    metadata.metadataConfidence = json.value("metadataConfidence").toString("none");
    metadata.matchedHashAlgorithm = json.value("matchedHashAlgorithm").toString(); metadata.matchedHash = json.value("matchedHash").toString();
    const QJsonObject provenance = json.value("fieldProvenance").toObject();
    for (auto it = provenance.begin(); it != provenance.end(); ++it) {
        const QJsonObject item = it.value().toObject();
        metadata.fieldProvenance.insert(it.key(), {item.value("source").toString(), item.value("confidence").toString()});
    }
    metadata.fetchedAt = QDateTime::fromString(json.value("fetchedAt").toString(), Qt::ISODate);
    metadata.expiresAt = QDateTime::fromString(json.value("expiresAt").toString(), Qt::ISODate);
    return metadata;
}

QString resultKindToString(MetadataResultKind kind) {
    switch (kind) { case MetadataResultKind::Match: return "match"; case MetadataResultKind::NoMatch: return "no-match";
    case MetadataResultKind::TemporaryError: return "temporary-error"; case MetadataResultKind::Unsupported: return "unsupported";
    case MetadataResultKind::PermanentError: return "permanent-error"; } return "permanent-error";
}

MetadataResultKind resultKindFromString(const QString& kind) {
    if (kind == "match") return MetadataResultKind::Match;
    if (kind == "no-match") return MetadataResultKind::NoMatch;
    if (kind == "temporary-error") return MetadataResultKind::TemporaryError;
    if (kind == "unsupported") return MetadataResultKind::Unsupported;
    return MetadataResultKind::PermanentError;
}

} // namespace LudoShelf::Metadata
