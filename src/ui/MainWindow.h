#ifndef LUDOSHELF_UI_MAINWINDOW_H
#define LUDOSHELF_UI_MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QLineEdit>
#include <QLabel>
#include <QStackedWidget>
#include <QMenu>
#include <QFutureWatcher>
#include <QFileSystemWatcher>
#include <QTimer>
#include <QHash>
#include <QQueue>
#include <QSet>
#include <QUuid>
#include <QCloseEvent>

class QFrame;
class QProgressBar;
class QResizeEvent;
class QAction;

#include "SystemsView.h"
#include "GamesTableView.h"
#include "GamesGridView.h"
#include "GameDetailsPanel.h"
#include "../models/SystemListModel.h"
#include "../models/GameTableModel.h"
#include "../models/GameFilterProxyModel.h"
#include "../launch/LaunchService.h"
#include "../covers/CoverAcquisitionService.h"
#include "../covers/LibretroThumbnailCatalog.h"
#include "../covers/RetroArchCoverDiscovery.h"
#include "../metadata/RomMetadataCoordinator.h"
#include "../metadata/RomMetadataRepository.h"
#include "../metadata/LibretroDatabaseBootstrapper.h"

namespace LudoShelf::UI {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onSystemSelected(const QUuid& systemId);
    void onGameActivated(const QUuid& gameId);
    void onGameSelected(const QUuid& gameId);
    void onSearchTextChanged(const QString& text);
    void onAddSystemClicked();
    void onEditSystemClicked(const QUuid& systemId = {});
    void onEditEmulatorClicked(const QUuid& systemId = {});
    void onRescanSystemClicked(const QUuid& systemId = {});
    void onDeleteSystemClicked(const QUuid& systemId = {});
    void onExportJsonClicked();
    void onImportJsonClicked();
    void onCreateBackupClicked();
    void onDatAuditClicked();
    void onDiagnosticsClicked();
    void onAuditMediaStorageClicked();
    void onEditGameClicked();
    void onTestLaunchClicked();
    void onToggleFavoriteClicked();
    void onRevealFileClicked();
    void onManageCoverArtClicked();
    void onFindCoverArtClicked();
    void onRefreshRomMetadataClicked(bool force = true);
    void showGameContextMenu(const QPoint& pos);

private:
    void setupUi();
    void setupMenuBar();
    void restoreWindowState();
    void saveWindowState() const;
    void loadSystems();
    void refreshScanRootWatchers();
    void loadGamesForSystem(const QUuid& systemId);
    void requestCoverArt(const QUuid& gameId, bool automatic,
                         const Covers::RetroArchDiscoveryContext *retroArchContext = nullptr);
    void rescanSystem(const QUuid& systemId);
    void scheduleEnrichment(const QList<QUuid>& gameIds, bool coverArt, bool metadata);
    void scheduleNextEnrichment();
    void processNextEnrichment();
    void prepareRetroArchContext();
    void finishMetadataBeforeCover(const QUuid& gameId);
    void tryBeginDeferredCoverAfterMetadata();
    void beginDeferredCoverBatch(const QList<QUuid>& gameIds);
    void completeDeferredCover(const QUuid& gameId);
    void showEnrichmentProgress(const QString& phase, int completed, int total);
    void hideEnrichmentProgressSoon();
    void positionEnrichmentProgress();
    void applyGridCoverAspectForSystem(const QUuid& systemId);
    void setGridCoverAspect(GamesGridView::CoverAspect aspect);

    struct PendingEnrichment {
        bool coverArt{false};
        bool metadata{false};
    };

    QSplitter *m_mainSplitter;
    SystemsView *m_systemsView;
    QStackedWidget *m_viewStack;
    GamesTableView *m_gamesTableView;
    GamesGridView *m_gamesGridView;
    GameDetailsPanel *m_detailsPanel;
    QLineEdit *m_searchEdit;
    QLabel *m_systemTitleLabel;
    QFrame *m_enrichmentProgressPanel{nullptr};
    QLabel *m_enrichmentProgressLabel{nullptr};
    QProgressBar *m_enrichmentProgressBar{nullptr};
    QAction *m_portraitCoverAspectAction{nullptr};
    QAction *m_landscapeCoverAspectAction{nullptr};

    Models::SystemListModel *m_systemListModel;
    Models::GameTableModel *m_gameTableModel;
    Models::GameFilterProxyModel *m_gameFilterProxyModel;

    Launch::LaunchService *m_launchService;
    Covers::CoverAcquisitionService *m_coverAcquisitionService;
    Covers::LibretroThumbnailCatalog *m_thumbnailCatalog;
    Metadata::RomMetadataCoordinator *m_romMetadataCoordinator;
    Metadata::LibretroDatabaseBootstrapper *m_libretroDatabaseBootstrapper;
    QFutureWatcher<Covers::RetroArchDiscoveryContext> *m_retroArchContextWatcher;
    QFileSystemWatcher *m_scanRootWatcher{nullptr};
    QTimer *m_scanWatchDebounce{nullptr};
    QHash<QString, QUuid> m_watchedSystemByPath;
    QSet<QUuid> m_pendingWatchedSystems;
    QSet<QUuid> m_activeRescans;
    Covers::RetroArchDiscoveryContext m_retroArchContext;
    QQueue<QUuid> m_enrichmentQueue;
    QHash<QUuid, PendingEnrichment> m_enrichmentByGame;
    // Collection imports are deliberately two-phase: cover lookup begins
    // only after every metadata request in the import has reached a terminal
    // state, so it can use canonical title/region information.
    QSet<QUuid> m_metadataAwaitingCover;
    QSet<QUuid> m_coverAfterMetadata;
    QSet<QUuid> m_coverAwaitingCompletion;
    int m_coverBatchTotal{0};
    bool m_coverDelayScheduled{false};
    bool m_metadataBarrierRecheckScheduled{false};
    bool m_retroArchContextReady{false};
    bool m_enrichmentTimerScheduled{false};
    QUuid m_currentSystemId;
    QUuid m_selectedGameId;
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_MAINWINDOW_H
