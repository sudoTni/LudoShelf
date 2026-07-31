#include "RomMetadataRepository.h"

#include "RomHashService.h"
#include "../database/DatabaseManager.h"

#include <QJsonDocument>
#include <QSqlError>
#include <QSqlQuery>

namespace LudoShelf::Metadata {

bool CachedMetadata::fresh() const {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (result.kind == MetadataResultKind::NoMatch) return negativeUntil.isValid() && negativeUntil > now;
    if (result.kind == MetadataResultKind::Match && result.metadata.normalizationVersion < RomMetadata::NormalizationVersion) return false;
    return expiresAt.isValid() && expiresAt > now;
}

CachedMetadata RomMetadataRepository::cached(const QUuid& gameId, const QString& provider) const {
    CachedMetadata cache;
    QSqlQuery query(Database::DatabaseManager::instance().connection());
    query.prepare("SELECT provider_record_id, normalized_json, raw_response_json, result_kind, identity_confidence, metadata_confidence, matched_hash_algorithm, matched_hash, fetched_at, expires_at, negative_until, last_error_code FROM rom_metadata_cache WHERE rom_id = :rom_id AND provider = :provider");
    query.bindValue(":rom_id", gameId.toString(QUuid::WithBraces)); query.bindValue(":provider", provider);
    if (!query.exec() || !query.next()) return cache;
    cache.exists = true;
    cache.result.kind = resultKindFromString(query.value(3).toString());
    cache.result.rawResponse = query.value(2).toString();
    cache.result.message = query.value(11).toString();
    cache.fetchedAt = QDateTime::fromString(query.value(8).toString(), Qt::ISODate);
    cache.expiresAt = QDateTime::fromString(query.value(9).toString(), Qt::ISODate);
    cache.negativeUntil = QDateTime::fromString(query.value(10).toString(), Qt::ISODate);
    const auto document = QJsonDocument::fromJson(query.value(1).toByteArray());
    if (document.isObject()) cache.result.metadata = fromJson(document.object());
    cache.result.metadata.romId = gameId.toString(QUuid::WithBraces);
    cache.result.metadata.provider = provider;
    cache.result.metadata.providerRecordId = query.value(0).toString();
    cache.result.metadata.identityConfidence = query.value(4).toString();
    cache.result.metadata.metadataConfidence = query.value(5).toString();
    cache.result.metadata.matchedHashAlgorithm = query.value(6).toString();
    cache.result.metadata.matchedHash = query.value(7).toString();
    cache.result.metadata.fetchedAt = cache.fetchedAt; cache.result.metadata.expiresAt = cache.expiresAt;
    return cache;
}

bool RomMetadataRepository::saveHashes(const QUuid& gameId, const QList<HashCandidate>& candidates, const QDateTime& sourceModifiedAt) const {
    QSqlDatabase db = Database::DatabaseManager::instance().connection();
    if (!db.transaction()) return false;
    QSqlQuery clear(db); clear.prepare("DELETE FROM rom_content_hash WHERE rom_id = :rom_id"); clear.bindValue(":rom_id", gameId.toString(QUuid::WithBraces));
    if (!clear.exec()) { db.rollback(); return false; }
    QSqlQuery query(db);
    query.prepare("INSERT INTO rom_content_hash (rom_id, payload_key, byte_size, crc32, md5, sha1, sha256, hash_schema_version, source_modified_at, computed_at) VALUES (:rom_id, :payload_key, :byte_size, :crc32, :md5, :sha1, :sha256, :version, :modified, :computed)");
    for (const auto& candidate : candidates) {
        query.bindValue(":rom_id", gameId.toString(QUuid::WithBraces)); query.bindValue(":payload_key", candidate.payloadKey);
        query.bindValue(":byte_size", candidate.byteSize); query.bindValue(":crc32", candidate.crc32); query.bindValue(":md5", candidate.md5);
        query.bindValue(":sha1", candidate.sha1); query.bindValue(":sha256", candidate.sha256); query.bindValue(":version", RomHashService::HashSchemaVersion);
        query.bindValue(":modified", sourceModifiedAt.toString(Qt::ISODate)); query.bindValue(":computed", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) { db.rollback(); return false; }
    }
    return db.commit();
}

QList<HashCandidate> RomMetadataRepository::hashes(const QUuid& gameId, const QDateTime& sourceModifiedAt) const {
    QList<HashCandidate> values;
    QSqlQuery query(Database::DatabaseManager::instance().connection());
    query.prepare("SELECT payload_key, byte_size, crc32, md5, sha1, sha256 FROM rom_content_hash WHERE rom_id = :rom_id AND source_modified_at = :modified AND hash_schema_version = :version");
    query.bindValue(":rom_id", gameId.toString(QUuid::WithBraces)); query.bindValue(":modified", sourceModifiedAt.toString(Qt::ISODate)); query.bindValue(":version", RomHashService::HashSchemaVersion);
    if (!query.exec()) return values;
    while (query.next()) values.append({query.value(0).toString(), query.value(1).toLongLong(), query.value(2).toString(), query.value(3).toString(), query.value(4).toString(), query.value(5).toString()});
    return values;
}

bool RomMetadataRepository::saveResult(const QUuid& gameId, const ProviderLookupResult& result) const {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const bool match = result.kind == MetadataResultKind::Match;
    const bool noMatch = result.kind == MetadataResultKind::NoMatch;
    const QDateTime expires = match ? now.addDays(PositiveCacheDays) : noMatch ? now.addDays(NegativeCacheDays) : result.metadata.expiresAt;
    QSqlQuery query(Database::DatabaseManager::instance().connection());
    query.prepare(R"(
        INSERT INTO rom_metadata_cache (rom_id, provider, provider_record_id, normalized_json, raw_response_json, result_kind, identity_confidence, metadata_confidence, matched_hash_algorithm, matched_hash, fetched_at, expires_at, negative_until, last_error_code, last_error_at)
        VALUES (:rom_id, :provider, :record_id, :normalized, :raw, :kind, :identity, :metadata_confidence, :algorithm, :hash, :fetched, :expires, :negative_until, :error, :error_at)
        ON CONFLICT(rom_id, provider) DO UPDATE SET
            provider_record_id=excluded.provider_record_id, normalized_json=excluded.normalized_json, raw_response_json=excluded.raw_response_json,
            result_kind=excluded.result_kind, identity_confidence=excluded.identity_confidence, metadata_confidence=excluded.metadata_confidence,
            matched_hash_algorithm=excluded.matched_hash_algorithm, matched_hash=excluded.matched_hash, fetched_at=excluded.fetched_at,
            expires_at=excluded.expires_at, negative_until=excluded.negative_until, last_error_code=excluded.last_error_code, last_error_at=excluded.last_error_at
    )");
    query.bindValue(":rom_id", gameId.toString(QUuid::WithBraces)); query.bindValue(":provider", result.metadata.provider.isEmpty() ? QStringLiteral("libretro-database-1.22.1") : result.metadata.provider);
    query.bindValue(":record_id", result.metadata.providerRecordId); query.bindValue(":normalized", match ? QJsonDocument(toJson(result.metadata)).toJson(QJsonDocument::Compact) : QByteArray());
    query.bindValue(":raw", result.rawResponse); query.bindValue(":kind", resultKindToString(result.kind)); query.bindValue(":identity", result.metadata.identityConfidence);
    query.bindValue(":metadata_confidence", result.metadata.metadataConfidence); query.bindValue(":algorithm", result.metadata.matchedHashAlgorithm); query.bindValue(":hash", result.metadata.matchedHash);
    query.bindValue(":fetched", now.toString(Qt::ISODate)); query.bindValue(":expires", expires.toString(Qt::ISODate)); query.bindValue(":negative_until", noMatch ? expires.toString(Qt::ISODate) : QString());
    query.bindValue(":error", result.message.left(300)); query.bindValue(":error_at", result.kind == MetadataResultKind::TemporaryError ? now.toString(Qt::ISODate) : QString());
    return query.exec();
}

} // namespace LudoShelf::Metadata
