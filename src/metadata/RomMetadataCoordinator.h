#ifndef LUDOSHELF_METADATA_ROMMETADATACOORDINATOR_H
#define LUDOSHELF_METADATA_ROMMETADATACOORDINATOR_H

#include "LibretroDatabaseProvider.h"
#include "RomMetadataRepository.h"

#include <QHash>
#include <QQueue>
#include <QSet>

#include "../domain/Game.h"
#include "../domain/GameFile.h"
#include "../domain/System.h"

namespace LudoShelf::Metadata {

class RomMetadataCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit RomMetadataCoordinator(QObject* parent = nullptr);
    void request(const Domain::Game& game, const Domain::GameFile& file, const Domain::System& system, bool force = false, bool hashIfNeeded = false);
    void enrich(const Domain::Game& game, const Domain::GameFile& file, const Domain::System& system);
    // True only once every hash and provider lookup launched by this
    // coordinator has reached a terminal result.
    bool isIdle() const;
signals:
    void metadataReady(const QUuid& gameId, const RomMetadata& metadata, bool stale);
    void metadataState(const QUuid& gameId, const QString& state, const QString& message);
private:
    struct Pending {
        Domain::Game game;
        Domain::GameFile file;
        Domain::System system;
        bool allowContentHashFallback{true};
    };
    RomMetadataRepository m_repository;
    LibretroDatabaseProvider m_provider;
    QHash<QString, QList<Pending>> m_waitingByFingerprint;
    QHash<QString, QString> m_requestByContentKey;
    QHash<QString, QString> m_contentKeyByRequest;
    QHash<QString, Pending> m_titleLookupByRequest;
    QSet<QUuid> m_titleLookupQueued;
    QQueue<Pending> m_hashQueue;
    QSet<QUuid> m_hashQueued;
    int m_activeHashes{0};
    void useCandidates(const Pending& pending, const QList<HashCandidate>& candidates);
    void requestTitleFallback(const Pending& pending);
    void startNextHash();
    bool enabled() const;
};

} // namespace LudoShelf::Metadata

#endif // LUDOSHELF_METADATA_ROMMETADATACOORDINATOR_H
