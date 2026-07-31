#include "CoverAcquisitionService.h"

#include "../media/MediaStorageManager.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace LudoShelf::Covers {
namespace {
constexpr int MaxConcurrentDownloads = 4;
constexpr int DownloadTimeoutMs = 6000;
}

CoverAcquisitionService::CoverAcquisitionService(QObject *parent) : QObject(parent) {}

void CoverAcquisitionService::download(const QUuid& gameId, const CoverCandidate& candidate) {
    downloadCandidates(gameId, {candidate});
}

void CoverAcquisitionService::downloadCandidates(const QUuid& gameId, const QList<CoverCandidate>& candidates) {
    if (gameId.isNull() || candidates.isEmpty()) {
        emit coverFailed(gameId, "No cover candidates were available.");
        return;
    }
    if (m_activeGames.isEmpty() && m_waitingGames.isEmpty()) {
        m_completedGames = 0;
        m_totalGames = 0;
        m_downloadedGames = 0;
        m_failedGames = 0;
        m_failureDetails.clear();
    }
    const bool isNewGame = !m_pendingCandidates.contains(gameId) && !m_queuedGames.contains(gameId) && !m_activeGames.contains(gameId);
    m_pendingCandidates.insert(gameId, candidates);
    if (!m_activeGames.contains(gameId) && !m_queuedGames.contains(gameId)) {
        m_waitingGames.enqueue(gameId);
        m_queuedGames.insert(gameId);
    }
    if (isNewGame) ++m_totalGames;
    emit queueProgress(m_completedGames, m_totalGames);
    startQueuedGames();
}

void CoverAcquisitionService::startQueuedGames() {
    while (m_activeGames.size() < MaxConcurrentDownloads && !m_waitingGames.isEmpty()) {
        const QUuid gameId = m_waitingGames.dequeue();
        m_queuedGames.remove(gameId);
        if (!m_pendingCandidates.contains(gameId) || m_pendingCandidates.value(gameId).isEmpty()) continue;
        m_activeGames.insert(gameId);
        startNextDownload(gameId);
    }
}

void CoverAcquisitionService::finishFailedGame(const QUuid& gameId, const QString& reason) {
    m_pendingCandidates.remove(gameId);
    m_activeGames.remove(gameId);
    ++m_completedGames;
    ++m_failedGames;
    m_failureDetails.append(QStringLiteral("%1: %2").arg(gameId.toString(QUuid::WithoutBraces), reason));
    emit coverFailed(gameId, reason);
    emit queueProgress(m_completedGames, m_totalGames);
    reportBatchIfFinished();
    startQueuedGames();
}

void CoverAcquisitionService::finishSuccessfulGame(const QUuid& gameId, const QString& sha256) {
    m_pendingCandidates.remove(gameId);
    m_activeGames.remove(gameId);
    ++m_completedGames;
    ++m_downloadedGames;
    emit coverDownloaded(gameId, sha256);
    emit queueProgress(m_completedGames, m_totalGames);
    reportBatchIfFinished();
    startQueuedGames();
}

void CoverAcquisitionService::reportBatchIfFinished() {
    if (m_totalGames <= 0 || m_completedGames != m_totalGames || !m_activeGames.isEmpty() || !m_waitingGames.isEmpty()) return;
    emit batchFinished(m_totalGames, m_downloadedGames, m_failedGames, m_failureDetails);
}

void CoverAcquisitionService::startNextDownload(const QUuid& gameId, const QString& previousError) {
    auto pending = m_pendingCandidates.find(gameId);
    if (pending == m_pendingCandidates.end() || pending->isEmpty()) {
        finishFailedGame(gameId, previousError.isEmpty() ? "No cover candidate succeeded." : previousError);
        return;
    }
    const CoverCandidate candidate = pending->takeFirst();
    const QUrl url = candidate.downloadUrl;
    if (!url.isValid() || url.scheme() != "https" || url.host().isEmpty()) {
        emit coverAttemptFinished(gameId, candidate, 0, "Cover source must use HTTPS.");
        startNextDownload(gameId, "Cover source must use HTTPS.");
        return;
    }
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "LudoShelf/0.1 cover-art-client");
    request.setRawHeader("Accept", "image/avif,image/webp,image/png,image/jpeg,*/*;q=0.5");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_network.get(request);
    auto *timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    timeout->start(DownloadTimeoutMs);
    connect(timeout, &QTimer::timeout, reply, [reply]() { reply->abort(); });
    auto bytes = std::make_shared<QByteArray>();
    connect(reply, &QNetworkReply::readyRead, this, [reply, bytes]() {
        bytes->append(reply->readAll());
        if (bytes->size() > 40 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, bytes, gameId, candidate]() {
        bytes->append(reply->readAll());
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        const QString error = reply->errorString();
        reply->deleteLater();
        if (status != 200 || bytes->isEmpty() || !contentType.startsWith("image/")) {
            const QString reason = QString("Cover download failed (%1): %2").arg(status).arg(error);
            emit coverAttemptFinished(gameId, candidate, status, reason);
            startNextDownload(gameId, reason);
            return;
        }
        const QString sha256 = Media::MediaStorageManager::instance().storeCoverCandidate(gameId, *bytes, candidate);
        if (sha256.isEmpty()) {
            const QString reason = QStringLiteral("Downloaded cover failed image validation.");
            emit coverAttemptFinished(gameId, candidate, status, reason);
            startNextDownload(gameId, reason);
        } else {
            emit coverAttemptFinished(gameId, candidate, status, {});
            finishSuccessfulGame(gameId, sha256);
        }
    });
}

} // namespace LudoShelf::Covers
