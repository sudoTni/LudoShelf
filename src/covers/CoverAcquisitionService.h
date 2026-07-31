#ifndef LUDOSHELF_COVERS_COVERACQUISITIONSERVICE_H
#define LUDOSHELF_COVERS_COVERACQUISITIONSERVICE_H

#include "CoverTypes.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QSet>

namespace LudoShelf::Covers {

class CoverAcquisitionService : public QObject {
    Q_OBJECT
public:
    explicit CoverAcquisitionService(QObject *parent = nullptr);
    void download(const QUuid& gameId, const CoverCandidate& candidate);
    void downloadCandidates(const QUuid& gameId, const QList<CoverCandidate>& candidates);

signals:
    void coverDownloaded(const QUuid& gameId, const QString& sha256);
    void coverFailed(const QUuid& gameId, const QString& reason);
    void coverAttemptFinished(const QUuid& gameId, const CoverCandidate& candidate, int httpStatus, const QString& reason);
    void queueProgress(int completed, int total);
    void batchFinished(int total, int downloaded, int failed, const QStringList& failures);

private:
    QNetworkAccessManager m_network;
    QHash<QUuid, QList<CoverCandidate>> m_pendingCandidates;
    QQueue<QUuid> m_waitingGames;
    QSet<QUuid> m_queuedGames;
    QSet<QUuid> m_activeGames;
    int m_completedGames{0};
    int m_totalGames{0};
    int m_downloadedGames{0};
    int m_failedGames{0};
    QStringList m_failureDetails;

    void startNextDownload(const QUuid& gameId, const QString& previousError = {});
    void startQueuedGames();
    void finishFailedGame(const QUuid& gameId, const QString& reason);
    void finishSuccessfulGame(const QUuid& gameId, const QString& sha256);
    void reportBatchIfFinished();
};

} // namespace LudoShelf::Covers

#endif // LUDOSHELF_COVERS_COVERACQUISITIONSERVICE_H
