#include "RomMetadataCoordinator.h"

#include "RomHashService.h"
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrentRun>

namespace LudoShelf::Metadata {

RomMetadataCoordinator::RomMetadataCoordinator(QObject* parent) : QObject(parent), m_provider(this) {
    connect(&m_provider, &RomMetadataProvider::lookupFinished, this, [this](const QUuid& requestId, const ProviderLookupResult& result) {
        const QString fingerprint = requestId.toString(QUuid::WithoutBraces);
        const auto titleLookup = m_titleLookupByRequest.find(fingerprint);
        if (titleLookup != m_titleLookupByRequest.end()) {
            const Pending pending = titleLookup.value();
            m_titleLookupByRequest.erase(titleLookup);
            m_titleLookupQueued.remove(pending.game.id);
            if (result.kind == MetadataResultKind::Match) {
                m_repository.saveResult(pending.game.id, result);
                auto metadata = result.metadata;
                metadata.romId = pending.game.id.toString(QUuid::WithBraces);
                emit metadataReady(pending.game.id, metadata, false);
            } else if (result.kind == MetadataResultKind::NoMatch) {
                if (!pending.allowContentHashFallback) {
                    // Do not repeatedly retry an old archive-hash no-match.
                    // RVZ/CSO bytes are not the canonical disc payload, so a
                    // content hash cannot provide a meaningful fallback.
                    ProviderLookupResult titleNoMatch = result;
                    titleNoMatch.metadata.identityConfidence = QStringLiteral("title-region-unverified-v3");
                    m_repository.saveResult(pending.game.id, titleNoMatch);
                    emit metadataState(pending.game.id, "no-match", "No unique title-and-region metadata match for this transformed disc image.");
                    return;
                }
                if (!m_hashQueued.contains(pending.game.id)) {
                    m_hashQueue.enqueue(pending);
                    m_hashQueued.insert(pending.game.id);
                }
                emit metadataState(pending.game.id, "loading", "No unique title match; attempting exact disc identification…");
                startNextHash();
            } else if (result.kind == MetadataResultKind::TemporaryError) {
                emit metadataState(pending.game.id, "offline", result.message);
            } else {
                emit metadataState(pending.game.id, "error", result.message);
            }
            return;
        }
        m_requestByContentKey.remove(m_contentKeyByRequest.take(fingerprint));
        const QList<Pending> pending = m_waitingByFingerprint.take(fingerprint);
        for (const auto& item : pending) {
            if (result.kind != MetadataResultKind::TemporaryError) m_repository.saveResult(item.game.id, result);
            if (result.kind == MetadataResultKind::Match) { auto metadata = result.metadata; metadata.romId = item.game.id.toString(QUuid::WithBraces); emit metadataReady(item.game.id, metadata, false); }
            else if (result.kind == MetadataResultKind::NoMatch) emit metadataState(item.game.id, "no-match", "No exact metadata match.");
            else if (result.kind == MetadataResultKind::TemporaryError) emit metadataState(item.game.id, "offline", result.message);
            else emit metadataState(item.game.id, "error", result.message);
        }
    });
}

bool RomMetadataCoordinator::enabled() const {
    return m_provider.isAvailable();
}

bool RomMetadataCoordinator::isIdle() const {
    return m_activeHashes == 0 && m_hashQueue.isEmpty() && m_hashQueued.isEmpty() &&
           m_titleLookupByRequest.isEmpty() && m_titleLookupQueued.isEmpty() &&
           m_waitingByFingerprint.isEmpty() && m_requestByContentKey.isEmpty() &&
           m_contentKeyByRequest.isEmpty();
}

void RomMetadataCoordinator::enrich(const Domain::Game& game, const Domain::GameFile& file, const Domain::System& system) {
    request(game, file, system, false, true);
}

void RomMetadataCoordinator::request(const Domain::Game& game, const Domain::GameFile& file, const Domain::System& system, bool force, bool hashIfNeeded) {
    if (!enabled()) { emit metadataState(game.id, "error", "Libretro database 1.22.1 is not installed."); return; }
    if (file.path.isEmpty() || !file.available) { emit metadataState(game.id, "unsupported", "ROM file is unavailable."); return; }
    const bool titleOnly = RomHashService::requiresTitleLookup(file.path);
    const auto stored = m_repository.hashes(game.id, file.modifiedTime);
    const auto cached = m_repository.cached(game.id, m_provider.id());
    if (!force && cached.exists && cached.result.kind == MetadataResultKind::Match)
        emit metadataReady(game.id, cached.result.metadata, !cached.fresh());
    if (!force && cached.exists && cached.fresh()) {
        // A negative result made with an older hash schema is not valid after
        // adding a canonical payload transform (for example Sega SMD).
        // A transformed-disc archive may have an old archive-byte no-match.
        // Route that once through its title/region lookup instead.
        const bool retryOldTransformedDiscNoMatch = titleOnly && cached.result.kind == MetadataResultKind::NoMatch &&
            cached.result.metadata.identityConfidence != QStringLiteral("title-region-unverified-v3");
        if (!retryOldTransformedDiscNoMatch && (cached.result.kind != MetadataResultKind::NoMatch || !stored.isEmpty())) {
            if (cached.result.kind == MetadataResultKind::NoMatch) emit metadataState(game.id, "no-match", "No exact metadata match.");
            return;
        }
    }
    const Pending pending{game, file, system, !titleOnly};
    if (titleOnly) {
        requestTitleFallback(pending);
        return;
    }
    if (!stored.isEmpty()) { useCandidates(pending, stored); return; }
    if (!hashIfNeeded) { emit metadataState(game.id, "unprepared", "ROM information has not been prepared. Use Refresh ROM Metadata."); return; }
    if (QFileInfo(file.path).suffix().compare("chd", Qt::CaseInsensitive) == 0) {
        requestTitleFallback(pending);
        return;
    }
    if (!m_hashQueued.contains(game.id)) { m_hashQueue.enqueue(pending); m_hashQueued.insert(game.id); }
    emit metadataState(game.id, "loading", "Identifying ROM by content hash…");
    startNextHash();
}

void RomMetadataCoordinator::requestTitleFallback(const Pending& pending) {
    if (m_titleLookupQueued.contains(pending.game.id)) return;
    const RomLookupContext context{pending.system.name.isEmpty() ? pending.system.shortName : pending.system.name,
                                   QFileInfo(pending.file.path).completeBaseName(), false,
                                   pending.system.shortName, pending.game.region};
    const QUuid requestId = m_provider.lookupByTitle(context);
    m_titleLookupByRequest.insert(requestId.toString(QUuid::WithoutBraces), pending);
    m_titleLookupQueued.insert(pending.game.id);
    emit metadataState(pending.game.id, "loading", "Looking up ROM information by unique title and region…");
}

void RomMetadataCoordinator::startNextHash() {
    while (m_activeHashes < 2 && !m_hashQueue.isEmpty()) {
        const Pending pending = m_hashQueue.dequeue();
        ++m_activeHashes;
        auto* watcher = new QFutureWatcher<RomHashBatch>(this);
        connect(watcher, &QFutureWatcher<RomHashBatch>::finished, this, [this, watcher, pending] {
            const RomHashBatch batch = watcher->result(); watcher->deleteLater(); --m_activeHashes; m_hashQueued.remove(pending.game.id);
            if (!batch.unsupportedReason.isEmpty()) emit metadataState(pending.game.id, "unsupported", batch.unsupportedReason);
            else if (batch.candidates.isEmpty()) emit metadataState(pending.game.id, "error", batch.error.isEmpty() ? "ROM hashes are unavailable." : batch.error);
            else { m_repository.saveHashes(pending.game.id, batch.candidates, pending.file.modifiedTime); useCandidates(pending, batch.candidates); }
            startNextHash();
        });
        watcher->setFuture(QtConcurrent::run(&RomHashService::discoverAndHash, pending.file.path, pending.file.modifiedTime, std::shared_ptr<std::atomic_bool>{}));
    }
}

void RomMetadataCoordinator::useCandidates(const Pending& pending, const QList<HashCandidate>& candidates) {
    QStringList values; for (const auto& candidate : candidates) values.append(candidate.sha256.isEmpty() ? candidate.sha1 : candidate.sha256); values.sort();
    const QString contentKey = values.join('|');
    if (contentKey.isEmpty()) { emit metadataState(pending.game.id, "error", "ROM hashes are unavailable."); return; }
    const auto activeRequest = m_requestByContentKey.constFind(contentKey);
    if (activeRequest != m_requestByContentKey.cend()) { m_waitingByFingerprint[activeRequest.value()].append(pending); return; }
    RomLookupContext context{pending.system.name.isEmpty() ? pending.system.shortName : pending.system.name,
                             pending.game.title, false, pending.system.shortName, pending.game.region};
    const QUuid requestId = m_provider.lookupByHashes(candidates, context);
    const QString requestKey = requestId.toString(QUuid::WithoutBraces);
    m_waitingByFingerprint.insert(requestKey, {pending});
    m_requestByContentKey.insert(contentKey, requestKey);
    m_contentKeyByRequest.insert(requestKey, contentKey);
    emit metadataState(pending.game.id, "loading", "Looking up ROM information…");
}

} // namespace LudoShelf::Metadata
