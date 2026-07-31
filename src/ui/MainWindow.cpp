#include "MainWindow.h"
#include "AddSystemWizard.h"
#include "EditSystemDialog.h"
#include "EditEmulatorDialog.h"
#include "TestLaunchDialog.h"
#include "EditGameDialog.h"
#include "DiagnosticsDialog.h"
#include "DatAuditDialog.h"
#include "CoverArtDialog.h"
#include "../app/AppPaths.h"
#include "../database/DatabaseManager.h"
#include "../app/LibraryBackupService.h"
#include "../covers/LibretroCoverProvider.h"
#include "../covers/LibretroThumbnailCatalog.h"
#include "../covers/LocalCoverDiscovery.h"
#include "../covers/RetroArchCoverDiscovery.h"
#include "../media/MediaStorageManager.h"
#include "../media/PlaceholderGenerator.h"
#include "../scanning/DirectoryScanner.h"
#include "../metadata/RomMetadataRepository.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QToolButton>
#include <QFile>
#include <QHash>
#include <QSet>
#include <QCloseEvent>
#include <QSettings>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPointer>
#include <QTimer>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QCheckBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFrame>
#include <QProgressBar>
#include <QResizeEvent>
#include <QActionGroup>

namespace LudoShelf::UI {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_systemListModel(new Models::SystemListModel(this))
    , m_gameTableModel(new Models::GameTableModel(this))
    , m_gameFilterProxyModel(new Models::GameFilterProxyModel(this))
    , m_launchService(new Launch::LaunchService(this))
    , m_coverAcquisitionService(new Covers::CoverAcquisitionService(this))
    , m_thumbnailCatalog(new Covers::LibretroThumbnailCatalog(this))
    , m_romMetadataCoordinator(new Metadata::RomMetadataCoordinator(this))
    , m_retroArchContextWatcher(new QFutureWatcher<Covers::RetroArchDiscoveryContext>(this))
    , m_scanRootWatcher(new QFileSystemWatcher(this))
    , m_scanWatchDebounce(new QTimer(this))
{
    m_gameFilterProxyModel->setSourceModel(m_gameTableModel);
    m_gameFilterProxyModel->sort(Models::GameTableModel::ColumnTitle, Qt::AscendingOrder);

    connect(m_retroArchContextWatcher, &QFutureWatcher<Covers::RetroArchDiscoveryContext>::finished, this, [this] {
        m_retroArchContext = m_retroArchContextWatcher->result();
        m_retroArchContextReady = true;
        scheduleNextEnrichment();
    });
    m_scanWatchDebounce->setSingleShot(true);
    m_scanWatchDebounce->setInterval(1250);
    connect(m_scanRootWatcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) {
        const QUuid systemId = m_watchedSystemByPath.value(QFileInfo(path).absoluteFilePath());
        if (!systemId.isNull()) m_pendingWatchedSystems.insert(systemId);
        if (!m_pendingWatchedSystems.isEmpty()) m_scanWatchDebounce->start();
    });
    connect(m_scanWatchDebounce, &QTimer::timeout, this, [this] {
        const QSet<QUuid> systems = m_pendingWatchedSystems;
        m_pendingWatchedSystems.clear();
        for (const QUuid& systemId : systems) rescanSystem(systemId);
    });

    setupUi();
    setupMenuBar();
    restoreWindowState();
    loadSystems();

    statusBar()->showMessage("LudoShelf Ready");

    connect(m_launchService, &Launch::LaunchService::gameStarted, this, [this](const QUuid& id, qint64 pid) {
        if (Database::DatabaseManager::instance().markGameLaunching(id) && id == m_selectedGameId) {
            loadGamesForSystem(m_currentSystemId);
            onGameSelected(id);
        }
        statusBar()->showMessage(QString("Game launched successfully (PID: %1)").arg(pid), 5000);
    });

    connect(m_launchService, &Launch::LaunchService::gameFinished, this, [this](const QUuid& id, int exitCode, QProcess::ExitStatus status, int duration) {
        Domain::PlaySession session;
        session.gameId = id;
        session.endedAt = QDateTime::currentDateTimeUtc();
        session.startedAt = session.endedAt.addSecs(-qMax(0, duration));
        session.durationSeconds = qMax(0, duration);
        session.exitCode = exitCode;
        session.exitStatus = static_cast<int>(status);
        const bool recorded = Database::DatabaseManager::instance().recordCompletedPlay(session);
        if (recorded && id == m_selectedGameId) {
            loadGamesForSystem(m_currentSystemId);
            onGameSelected(id);
        }
        statusBar()->showMessage(QString("Game process finished with code %1 (Duration: %2s)").arg(exitCode).arg(duration), 5000);
    });

    connect(m_launchService, &Launch::LaunchService::launchFailed, this, [this](const QUuid& id, const QString& error) {
        Q_UNUSED(id);
        QMessageBox::critical(this, "Launch Failure", error);
        statusBar()->showMessage("Launch failed: " + error, 5000);
    });
    connect(m_launchService, &Launch::LaunchService::frontendVisibilityRequested, this,
            [this](Domain::HidePolicy policy, bool restore) {
        if (restore) {
            if (isHidden() || isMinimized()) {
                showNormal();
                raise();
                activateWindow();
            }
            return;
        }
        if (policy == Domain::HidePolicy::Minimize) showMinimized();
        else if (policy == Domain::HidePolicy::Hide) hide();
    });

    connect(m_coverAcquisitionService, &Covers::CoverAcquisitionService::coverDownloaded, this,
            [this](const QUuid& gameId, const QString&) {
        completeDeferredCover(gameId);
        const bool refreshSelectedGame = gameId == m_selectedGameId;
        loadGamesForSystem(m_currentSystemId);
        if (refreshSelectedGame) onGameSelected(gameId);
        statusBar()->showMessage("Cover art downloaded.", 4000);
    });
    connect(m_coverAcquisitionService, &Covers::CoverAcquisitionService::coverFailed, this,
            [this](const QUuid& gameId, const QString& reason) {
        completeDeferredCover(gameId);
        statusBar()->showMessage("No cover downloaded: " + reason, 6000);
    });
    connect(m_coverAcquisitionService, &Covers::CoverAcquisitionService::coverAttemptFinished, this,
            [](const QUuid& gameId, const Covers::CoverCandidate& candidate, int status, const QString& reason) {
        Covers::CoverProvider provider;
        provider.id = candidate.providerId;
        provider.displayName = candidate.providerId;
        provider.adapterVersion = QStringLiteral("1.0");
        provider.stability = QStringLiteral("public-supported");
        provider.priority = static_cast<int>(candidate.sourcePriority);
        Database::DatabaseManager::instance().saveCoverProvider(provider);
        Covers::CoverJob job;
        job.gameId = gameId;
        job.providerId = candidate.providerId;
        job.operation = QStringLiteral("download_cover");
        job.state = reason.isEmpty() ? QStringLiteral("completed") : QStringLiteral("failed");
        job.attemptCount = 1;
        job.lastHttpStatus = status;
        job.lastErrorCode = reason.isEmpty() ? QString() : QString::number(status);
        job.lastErrorMessage = reason;
        job.requestJson = QString::fromUtf8(QJsonDocument(QJsonObject{{"url", candidate.downloadUrl.toString()}, {"title", candidate.providerTitle}}).toJson(QJsonDocument::Compact));
        job.resultJson = QString::fromUtf8(QJsonDocument(QJsonObject{{"httpStatus", status}, {"error", reason}}).toJson(QJsonDocument::Compact));
        job.completedAt = QDateTime::currentDateTimeUtc();
        Database::DatabaseManager::instance().saveCoverJob(job);
    });
    connect(m_coverAcquisitionService, &Covers::CoverAcquisitionService::queueProgress, this,
            [this](int completed, int total) {
        if (!m_coverAwaitingCompletion.isEmpty())
            showEnrichmentProgress(QStringLiteral("Downloading cover art"),
                                   m_coverBatchTotal - static_cast<int>(m_coverAwaitingCompletion.size()), m_coverBatchTotal);
        if (total > 0 && completed >= total) {
            statusBar()->showMessage(QString("Cover-art lookup complete: %1 games processed.").arg(total), 5000);
            return;
        }
        statusBar()->showMessage(QString("Cover-art lookup: %1 of %2 games processed.").arg(completed).arg(total));
    });
    connect(m_coverAcquisitionService, &Covers::CoverAcquisitionService::batchFinished, this,
            [this](int total, int downloaded, int failed, const QStringList& failures) {
        QString summary = QString("Cover-art lookup complete: %1 downloaded, %2 failed (%3 total).").arg(downloaded).arg(failed).arg(total);
        if (!failures.isEmpty()) summary += QString(" First failure: %1").arg(failures.first());
        statusBar()->showMessage(summary, 15000);
    });
    connect(m_romMetadataCoordinator, &Metadata::RomMetadataCoordinator::metadataReady, this,
            [this](const QUuid& gameId, const Metadata::RomMetadata& metadata, bool stale) {
        const QDate releaseDate = metadata.releaseYear > 0 ? QDate(metadata.releaseYear, 1, 1) : QDate();
        const QString region = metadata.regions.isEmpty() ? QString() : metadata.regions.first();
        if (Database::DatabaseManager::instance().fillGameMetadataFields(gameId, releaseDate, metadata.developer, region)) {
            const Domain::Game updatedGame = Database::DatabaseManager::instance().getGame(gameId);
            if (updatedGame.systemId == m_currentSystemId) m_gameTableModel->updateGame(updatedGame);
        }
        if (gameId == m_selectedGameId) m_detailsPanel->setRomMetadata(metadata, stale);
        finishMetadataBeforeCover(gameId);
    });
    connect(m_romMetadataCoordinator, &Metadata::RomMetadataCoordinator::metadataState, this,
            [this](const QUuid& gameId, const QString& state, const QString& message) {
        if (gameId == m_selectedGameId) m_detailsPanel->setRomMetadataState(state, message);
        if (state != QStringLiteral("loading")) finishMetadataBeforeCover(gameId);
    });
}

void MainWindow::restoreWindowState() {
    QSettings settings(App::AppPaths::settingsPath(), QSettings::IniFormat);
    const QByteArray geometry = settings.value("MainWindow/Geometry").toByteArray();
    if (!geometry.isEmpty()) restoreGeometry(geometry);
    const QByteArray state = settings.value("MainWindow/State").toByteArray();
    if (!state.isEmpty()) restoreState(state);
    const QList<int> splitterSizes = settings.value("MainWindow/MainSplitterSizes").value<QList<int>>();
    if (!splitterSizes.isEmpty()) m_mainSplitter->setSizes(splitterSizes);
    const int viewIndex = settings.value("MainWindow/GameViewIndex", 0).toInt();
    if (viewIndex >= 0 && viewIndex < m_viewStack->count()) m_viewStack->setCurrentIndex(viewIndex);
}

void MainWindow::saveWindowState() const {
    QSettings settings(App::AppPaths::settingsPath(), QSettings::IniFormat);
    settings.setValue("MainWindow/Geometry", saveGeometry());
    settings.setValue("MainWindow/State", saveState());
    settings.setValue("MainWindow/MainSplitterSizes", QVariant::fromValue(m_mainSplitter->sizes()));
    settings.setValue("MainWindow/GameViewIndex", m_viewStack->currentIndex());
}

void MainWindow::closeEvent(QCloseEvent *event) {
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    positionEnrichmentProgress();
}

void MainWindow::positionEnrichmentProgress() {
    if (!m_enrichmentProgressPanel || !centralWidget()) return;
    const QSize size = m_enrichmentProgressPanel->sizeHint();
    m_enrichmentProgressPanel->resize(size);
    m_enrichmentProgressPanel->move(qMax(12, centralWidget()->width() - size.width() - 16),
                                    qMax(12, centralWidget()->height() - size.height() - 16));
    m_enrichmentProgressPanel->raise();
}

void MainWindow::showEnrichmentProgress(const QString& phase, int completed, int total) {
    if (!m_enrichmentProgressPanel || total <= 0) return;
    const int clampedCompleted = qBound(0, completed, total);
    m_enrichmentProgressLabel->setText(phase);
    m_enrichmentProgressBar->setRange(0, total);
    m_enrichmentProgressBar->setValue(clampedCompleted);
    m_enrichmentProgressBar->setFormat(QStringLiteral("%1 of %2").arg(clampedCompleted).arg(total));
    positionEnrichmentProgress();
    m_enrichmentProgressPanel->show();
}

void MainWindow::hideEnrichmentProgressSoon() {
    QTimer::singleShot(2500, this, [this] {
        if (m_metadataAwaitingCover.isEmpty() && m_coverAwaitingCompletion.isEmpty() && m_enrichmentProgressPanel)
            m_enrichmentProgressPanel->hide();
    });
}

void MainWindow::applyGridCoverAspectForSystem(const QUuid& systemId) {
    if (systemId.isNull()) return;
    QSettings settings(App::AppPaths::settingsPath(), QSettings::IniFormat);
    const QString key = QStringLiteral("SystemViews/%1/GridCoverAspect")
        .arg(systemId.toString(QUuid::WithoutBraces));
    const bool landscape = settings.value(key, QStringLiteral("portrait")).toString() == QStringLiteral("landscape");
    const auto aspect = landscape ? GamesGridView::CoverAspect::Landscape : GamesGridView::CoverAspect::Portrait;
    m_gamesGridView->setCoverAspect(aspect);
    if (m_portraitCoverAspectAction) m_portraitCoverAspectAction->setChecked(!landscape);
    if (m_landscapeCoverAspectAction) m_landscapeCoverAspectAction->setChecked(landscape);
}

void MainWindow::setGridCoverAspect(GamesGridView::CoverAspect aspect) {
    if (m_currentSystemId.isNull()) return;
    m_gamesGridView->setCoverAspect(aspect);
    QSettings settings(App::AppPaths::settingsPath(), QSettings::IniFormat);
    const QString key = QStringLiteral("SystemViews/%1/GridCoverAspect")
        .arg(m_currentSystemId.toString(QUuid::WithoutBraces));
    settings.setValue(key, aspect == GamesGridView::CoverAspect::Landscape ? QStringLiteral("landscape") : QStringLiteral("portrait"));
    if (m_portraitCoverAspectAction) m_portraitCoverAspectAction->setChecked(aspect == GamesGridView::CoverAspect::Portrait);
    if (m_landscapeCoverAspectAction) m_landscapeCoverAspectAction->setChecked(aspect == GamesGridView::CoverAspect::Landscape);
}

void MainWindow::setupUi() {
    setWindowTitle("LudoShelf — Qt Game Library & Emulator Frontend");
    resize(1150, 720);

    auto *centralWidget = new QWidget(this);
    setCentralWidget(centralWidget);

    auto *mainLayout = new QHBoxLayout(centralWidget);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    m_mainSplitter = new QSplitter(Qt::Horizontal, centralWidget);
    mainLayout->addWidget(m_mainSplitter);

    // Left pane: Systems
    m_systemsView = new SystemsView(m_mainSplitter);
    m_systemsView->setModel(m_systemListModel);
    m_mainSplitter->addWidget(m_systemsView);

    // Right pane: Games header + Stacked View + Details
    auto *rightWidget = new QWidget(m_mainSplitter);
    auto *rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(4, 4, 4, 4);

    // Control bar
    auto *topBarLayout = new QHBoxLayout();
    m_systemTitleLabel = new QLabel("All Games", rightWidget);
    m_systemTitleLabel->setStyleSheet("font-size: 18px; font-weight: bold;");
    topBarLayout->addWidget(m_systemTitleLabel);

    topBarLayout->addStretch();

    // View Switcher buttons
    auto *tableBtn = new QToolButton(rightWidget);
    tableBtn->setText("Table");
    topBarLayout->addWidget(tableBtn);

    auto *gridBtn = new QToolButton(rightWidget);
    gridBtn->setText("Grid");
    topBarLayout->addWidget(gridBtn);

    m_searchEdit = new QLineEdit(rightWidget);
    m_searchEdit->setPlaceholderText("Search titles, developers (Ctrl+F)...");
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setMaximumWidth(280);
    topBarLayout->addWidget(m_searchEdit);

    rightLayout->addLayout(topBarLayout);

    // Splitter for Stacked View (Table/Grid) and Details Panel
    auto *gamesSplitter = new QSplitter(Qt::Horizontal, rightWidget);

    m_viewStack = new QStackedWidget(gamesSplitter);

    m_gamesTableView = new GamesTableView(m_viewStack);
    m_gamesTableView->setProxyModel(m_gameFilterProxyModel);
    m_gamesTableView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_viewStack->addWidget(m_gamesTableView);

    m_gamesGridView = new GamesGridView(m_viewStack);
    m_gamesGridView->setProxyModel(m_gameFilterProxyModel);
    m_gamesGridView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_viewStack->addWidget(m_gamesGridView);

    gamesSplitter->addWidget(m_viewStack);

    m_detailsPanel = new GameDetailsPanel(gamesSplitter);
    gamesSplitter->addWidget(m_detailsPanel);

    gamesSplitter->setSizes({720, 280});
    rightLayout->addWidget(gamesSplitter);

    m_mainSplitter->addWidget(rightWidget);
    m_mainSplitter->setSizes({240, 910});

    // This deliberately floats above the library rather than consuming a row
    // in the main layout.  Imports can take a while, but the library remains
    // usable and the active enrichment phase stays visible.
    m_enrichmentProgressPanel = new QFrame(centralWidget);
    m_enrichmentProgressPanel->setObjectName(QStringLiteral("enrichmentProgressPanel"));
    m_enrichmentProgressPanel->setFrameShape(QFrame::StyledPanel);
    m_enrichmentProgressPanel->setStyleSheet(
        "QFrame#enrichmentProgressPanel { background: palette(base); border: 1px solid palette(mid); border-radius: 6px; }"
        "QLabel { font-weight: 600; }");
    auto *progressLayout = new QVBoxLayout(m_enrichmentProgressPanel);
    progressLayout->setContentsMargins(12, 8, 12, 9);
    progressLayout->setSpacing(5);
    m_enrichmentProgressLabel = new QLabel(m_enrichmentProgressPanel);
    m_enrichmentProgressBar = new QProgressBar(m_enrichmentProgressPanel);
    m_enrichmentProgressBar->setTextVisible(true);
    m_enrichmentProgressBar->setMinimumWidth(300);
    progressLayout->addWidget(m_enrichmentProgressLabel);
    progressLayout->addWidget(m_enrichmentProgressBar);
    m_enrichmentProgressPanel->adjustSize();
    m_enrichmentProgressPanel->hide();

    // Connections
    connect(tableBtn, &QToolButton::clicked, this, [this]() { m_viewStack->setCurrentIndex(0); });
    connect(gridBtn, &QToolButton::clicked, this, [this]() { m_viewStack->setCurrentIndex(1); });

    connect(m_systemsView, &SystemsView::systemSelected, this, &MainWindow::onSystemSelected);
    connect(m_systemsView, &SystemsView::addSystemRequested, this, &MainWindow::onAddSystemClicked);
    connect(m_systemsView, &SystemsView::editSystemRequested, this, &MainWindow::onEditSystemClicked);
    connect(m_systemsView, &SystemsView::editEmulatorRequested, this, &MainWindow::onEditEmulatorClicked);
    connect(m_systemsView, &SystemsView::rescanSystemRequested, this, &MainWindow::onRescanSystemClicked);
    connect(m_systemsView, &SystemsView::deleteSystemRequested, this, &MainWindow::onDeleteSystemClicked);

    connect(m_gamesTableView, &GamesTableView::gameActivated, this, &MainWindow::onGameActivated);
    connect(m_gamesTableView, &GamesTableView::gameSelectionChanged, this, &MainWindow::onGameSelected);
    connect(m_gamesTableView, &GamesTableView::customContextMenuRequested, this, &MainWindow::showGameContextMenu);

    connect(m_gamesGridView, &GamesGridView::gameActivated, this, &MainWindow::onGameActivated);
    connect(m_gamesGridView, &GamesGridView::gameSelectionChanged, this, &MainWindow::onGameSelected);
    connect(m_gamesGridView, &GamesGridView::customContextMenuRequested, this, &MainWindow::showGameContextMenu);

    connect(m_detailsPanel, &GameDetailsPanel::launchRequested, this, &MainWindow::onGameActivated);
    connect(m_detailsPanel, &GameDetailsPanel::romMetadataRetryRequested, this, [this](const QUuid& gameId) {
        const auto game = Database::DatabaseManager::instance().getGame(gameId);
        if (!game.id.isNull()) {
            m_romMetadataCoordinator->request(game, Database::DatabaseManager::instance().getPrimaryFileForGame(gameId),
                                               Database::DatabaseManager::instance().getSystem(game.systemId), true, true);
        }
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged);
}

void MainWindow::setupMenuBar() {
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Add System / Import Wizard...", QKeySequence("Ctrl+N"), this, &MainWindow::onAddSystemClicked);
    fileMenu->addAction("&Edit Current System Properties...", this, [this]() { onEditSystemClicked(); });
    fileMenu->addAction("Configure System &Emulator Profile...", this, [this]() { onEditEmulatorClicked(); });
    fileMenu->addAction("&Rescan Current System ROM Folders", this, [this]() { onRescanSystemClicked(); });
    fileMenu->addSeparator();
    fileMenu->addAction("&Export Library to JSON...", this, &MainWindow::onExportJsonClicked);
    fileMenu->addAction("&Import Library from JSON...", this, &MainWindow::onImportJsonClicked);
    fileMenu->addAction("Create Database &Backup", this, &MainWindow::onCreateBackupClicked);
    fileMenu->addSeparator();
    fileMenu->addAction("&Quit", QKeySequence("Ctrl+Q"), qApp, &QApplication::quit);

    QMenu *viewMenu = menuBar()->addMenu("&View");
    viewMenu->addAction("&Table View", QKeySequence("Ctrl+1"), this, [this]() { m_viewStack->setCurrentIndex(0); });
    viewMenu->addAction("&Artwork Grid View", QKeySequence("Ctrl+3"), this, [this]() { m_viewStack->setCurrentIndex(1); });
    QMenu *coverAspectMenu = viewMenu->addMenu("Cover Art Display Aspect");
    auto *coverAspectGroup = new QActionGroup(this);
    coverAspectGroup->setExclusive(true);
    m_portraitCoverAspectAction = coverAspectMenu->addAction("Portrait Covers");
    m_portraitCoverAspectAction->setCheckable(true);
    m_landscapeCoverAspectAction = coverAspectMenu->addAction("Landscape Covers");
    m_landscapeCoverAspectAction->setCheckable(true);
    coverAspectGroup->addAction(m_portraitCoverAspectAction);
    coverAspectGroup->addAction(m_landscapeCoverAspectAction);
    connect(m_portraitCoverAspectAction, &QAction::triggered, this, [this] {
        setGridCoverAspect(GamesGridView::CoverAspect::Portrait);
    });
    connect(m_landscapeCoverAspectAction, &QAction::triggered, this, [this] {
        setGridCoverAspect(GamesGridView::CoverAspect::Landscape);
    });
    viewMenu->addSeparator();
    viewMenu->addAction("Reset Table Column &Widths", this, [this]() { m_gamesTableView->resetColumnWidths(); });


    QMenu *toolsMenu = menuBar()->addMenu("&Tools");
    toolsMenu->addAction("&DAT Audit && Collection Verification...", this, &MainWindow::onDatAuditClicked);
    toolsMenu->addAction("Fetch Missing Cover Art for Current System", this, &MainWindow::onFindCoverArtClicked);
    toolsMenu->addAction("Refresh ROM Metadata for Current System", this, [this] { onRefreshRomMetadataClicked(true); });

    QMenu *helpMenu = menuBar()->addMenu("&Help");
    helpMenu->addAction("System &Diagnostics...", this, &MainWindow::onDiagnosticsClicked);
    helpMenu->addSeparator();
    helpMenu->addAction("&About LudoShelf", this, [this]() {
        QMessageBox::about(this, "About LudoShelf",
            "<h3>LudoShelf v0.1.0</h3>"
            "<p>Qt Game Library & Emulator Frontend</p>"
            "<p>Developed by sudoTni</p>"
            "<p>Built with C++20 and Qt 6</p>");
    });
}

void MainWindow::loadSystems() {
    auto systems = Database::DatabaseManager::instance().getSystems();
    m_systemListModel->setSystems(systems);
    refreshScanRootWatchers();

    if (!systems.isEmpty() && m_currentSystemId.isNull()) {
        onSystemSelected(systems.first().id);
    }
}

void MainWindow::refreshScanRootWatchers() {
    const QStringList existing = m_scanRootWatcher->directories();
    if (!existing.isEmpty()) m_scanRootWatcher->removePaths(existing);
    m_watchedSystemByPath.clear();
    QStringList paths;
    for (const Domain::System& system : Database::DatabaseManager::instance().getSystems()) {
        for (const Database::ScanRoot& root : Database::DatabaseManager::instance().getScanRoots(system.id)) {
            const QString path = QFileInfo(root.path).absoluteFilePath();
            if (!root.watchChanges || !QFileInfo(path).isDir() || m_watchedSystemByPath.contains(path)) continue;
            m_watchedSystemByPath.insert(path, system.id);
            paths.append(path);
        }
    }
    if (!paths.isEmpty()) m_scanRootWatcher->addPaths(paths);
}

void MainWindow::onSystemSelected(const QUuid& systemId) {
    m_currentSystemId = systemId;
    m_gameFilterProxyModel->setSystemFilter(systemId);
    applyGridCoverAspectForSystem(systemId);

    auto systems = Database::DatabaseManager::instance().getSystems();
    for (const auto& sys : systems) {
        if (sys.id == systemId) {
            m_systemTitleLabel->setText(QString("%1 (%2 games)").arg(sys.name).arg(sys.gameCount));
            break;
        }
    }

    loadGamesForSystem(systemId);
}

void MainWindow::loadGamesForSystem(const QUuid& systemId) {
    auto games = Database::DatabaseManager::instance().getGamesForSystem(systemId);
    Metadata::RomMetadataRepository metadataRepository;
    bool refreshedFromCache = false;
    for (const Domain::Game& game : games) {
        const auto cached = metadataRepository.cached(game.id);
        if (cached.result.kind != Metadata::MetadataResultKind::Match) continue;
        const auto& metadata = cached.result.metadata;
        const QDate releaseDate = metadata.releaseYear > 0 ? QDate(metadata.releaseYear, 1, 1) : QDate();
        const QString region = metadata.regions.isEmpty() ? QString() : metadata.regions.first();
        const bool needsReleaseDate = !game.releaseDate.isValid() && releaseDate.isValid();
        const bool needsDeveloper = game.developer.trimmed().isEmpty() && !metadata.developer.trimmed().isEmpty();
        const bool needsRegion = game.region.trimmed().isEmpty() && !region.trimmed().isEmpty();
        if (needsReleaseDate || needsDeveloper || needsRegion) {
            Database::DatabaseManager::instance().fillGameMetadataFields(game.id, releaseDate, metadata.developer, region);
            refreshedFromCache = true;
        }
    }
    if (refreshedFromCache) games = Database::DatabaseManager::instance().getGamesForSystem(systemId);
    m_gameTableModel->setGames(games);
    m_detailsPanel->clear();
    m_selectedGameId = {};
}

void MainWindow::onGameActivated(const QUuid& gameId) {
    if (gameId.isNull()) return;

    Domain::Game targetGame = Database::DatabaseManager::instance().getGame(gameId);
    if (targetGame.id.isNull()) return;

    Domain::System targetSys = Database::DatabaseManager::instance().getSystem(targetGame.systemId);

    Domain::EmulatorProfile emuProfile;
    if (!targetGame.emulatorOverrideId.isNull()) {
        emuProfile = Database::DatabaseManager::instance().getEmulator(targetGame.emulatorOverrideId);
    }

    if (emuProfile.program.isEmpty()) {
        QUuid defaultEmuId = Database::DatabaseManager::instance().getSystemDefaultEmulator(targetSys.id);
        if (!defaultEmuId.isNull()) {
            emuProfile = Database::DatabaseManager::instance().getEmulator(defaultEmuId);
        }
    }

    if (emuProfile.program.isEmpty()) {
        emuProfile.name = "Default Launcher";
        emuProfile.program = "echo";
    }

    Domain::GameFile primaryFile = Database::DatabaseManager::instance().getPrimaryFileForGame(targetGame.id);

    statusBar()->showMessage(QString("Launching %1...").arg(targetGame.title));
    m_launchService->launchGame(targetGame, primaryFile, targetSys, emuProfile);
}

void MainWindow::onGameSelected(const QUuid& gameId) {
    m_selectedGameId = gameId;
    if (gameId.isNull()) {
        m_detailsPanel->clear();
        return;
    }

    Domain::Game g = Database::DatabaseManager::instance().getGame(gameId);
    if (!g.id.isNull()) {
        m_detailsPanel->setGame(g);
        m_romMetadataCoordinator->request(g, Database::DatabaseManager::instance().getPrimaryFileForGame(gameId),
                                          Database::DatabaseManager::instance().getSystem(g.systemId), false, true);
    }
}

void MainWindow::onSearchTextChanged(const QString& text) {
    m_gameFilterProxyModel->setSearchText(text);
}

void MainWindow::onAddSystemClicked() {
    AddSystemWizard wizard(this);
    if (wizard.exec() == QDialog::Accepted) {
        loadSystems();
        onSystemSelected(wizard.createdSystem().id);
        scheduleEnrichment(wizard.importedGameIds(), true, true);
        statusBar()->showMessage("System added. Artwork and ROM metadata are being prepared in the background.", 5000);
    }
}

void MainWindow::onEditSystemClicked(const QUuid& systemId) {
    QUuid targetId = systemId.isNull() ? m_currentSystemId : systemId;
    if (targetId.isNull()) {
        QMessageBox::warning(this, "Select System", "No system selected to edit.");
        return;
    }

    Domain::System sys = Database::DatabaseManager::instance().getSystem(targetId);
    if (sys.id.isNull()) return;

    EditSystemDialog dialog(sys, this);
    if (dialog.exec() == QDialog::Accepted) {
        Domain::System updated = dialog.updatedSystem();
        Database::DatabaseManager::instance().saveSystem(updated);
        if (dialog.hasConfiguredScanRoot()) {
            Database::DatabaseManager::instance().saveScanRoot(dialog.updatedScanRoot());
        } else if (dialog.hadScanRoot()) {
            Database::DatabaseManager::instance().deleteScanRoot(dialog.updatedScanRoot().id);
        }
        loadSystems();
        onSystemSelected(updated.id);
        statusBar()->showMessage(QString("System '%1' updated.").arg(updated.name), 3000);
    }
}

void MainWindow::onRescanSystemClicked(const QUuid& systemId) {
    const QUuid targetId = systemId.isNull() ? m_currentSystemId : systemId;
    if (targetId.isNull()) {
        QMessageBox::warning(this, "Select System", "Select a system before rescanning ROM folders.");
        return;
    }
    rescanSystem(targetId);
}

void MainWindow::rescanSystem(const QUuid& systemId) {
    const Domain::System system = Database::DatabaseManager::instance().getSystem(systemId);
    const auto roots = Database::DatabaseManager::instance().getScanRoots(systemId);
    if (system.id.isNull() || roots.isEmpty()) {
        QMessageBox::information(this, "No ROM Folder", "This system has no ROM folder configured. Edit its properties to add one.");
        return;
    }

    QHash<QString, QUuid> knownPaths;
    const auto existingGames = Database::DatabaseManager::instance().getGamesForSystem(systemId);
    for (const auto& game : existingGames) {
        const auto file = Database::DatabaseManager::instance().getPrimaryFileForGame(game.id);
        if (!file.path.isEmpty()) knownPaths.insert(QFileInfo(file.path).absoluteFilePath(), game.id);
    }

    Scanning::DirectoryScanner scanner;
    QList<QPair<Domain::Game, Domain::GameFile>> newGames;
    int refreshedGames = 0;
    for (const auto& root : roots) {
        if (root.path.isEmpty() || !QFileInfo(root.path).isDir()) continue;
        Scanning::ScanOptions options;
        options.allowedExtensions = root.includeExtensions;
        options.excludedExtensions = root.excludeExtensions;
        options.excludedPatterns = root.excludePatterns;
        options.recursive = root.recursive;
        options.followSymlinks = root.followSymlinks;
        const auto candidates = scanner.scanDirectory(systemId, root.path, options);
        for (const auto& candidate : candidates) {
            const QString path = QFileInfo(candidate.file.path).absoluteFilePath();
            if (knownPaths.contains(path)) {
                if (Database::DatabaseManager::instance().updateScannedGame(knownPaths.value(path), candidate.game, candidate.file)) ++refreshedGames;
                continue;
            }
            knownPaths.insert(path, candidate.game.id);
            newGames.append({candidate.game, candidate.file});
        }
    }

    if (newGames.isEmpty()) {
        statusBar()->showMessage(QString("ROM rescan complete: refreshed %1 existing games; no new games found.").arg(refreshedGames), 5000);
        return;
    }
    if (!Database::DatabaseManager::instance().saveGamesBatch(newGames)) {
        QMessageBox::critical(this, "Rescan Failed", "New ROM files could not be saved to the library.");
        return;
    }
    QList<QUuid> newGameIds;
    for (const auto& entry : newGames) newGameIds.append(entry.first.id);
    scheduleEnrichment(newGameIds, true, true);
    loadSystems();
    onSystemSelected(systemId);
    statusBar()->showMessage(QString("ROM rescan complete: added %1 new games and refreshed %2 existing games.")
        .arg(newGames.size()).arg(refreshedGames), 6000);
}

void MainWindow::onEditEmulatorClicked(const QUuid& systemId) {
    QUuid targetId = systemId.isNull() ? m_currentSystemId : systemId;
    if (targetId.isNull()) {
        QMessageBox::warning(this, "Select System", "No system selected for emulator configuration.");
        return;
    }

    Domain::System sys = Database::DatabaseManager::instance().getSystem(targetId);
    if (sys.id.isNull()) return;

    Domain::EmulatorProfile emuProfile;
    QUuid defaultEmuId = Database::DatabaseManager::instance().getSystemDefaultEmulator(sys.id);
    if (!defaultEmuId.isNull()) {
        emuProfile = Database::DatabaseManager::instance().getEmulator(defaultEmuId);
    }

    if (emuProfile.program.isEmpty()) {
        emuProfile.name = sys.name + " Emulator";
        emuProfile.program = "retroarch";
        Domain::ArgumentTemplate arg;
        arg.templateString = "{game.path}";
        emuProfile.arguments.append(arg);
    }

    EditEmulatorDialog dialog(emuProfile, this);
    if (dialog.exec() == QDialog::Accepted) {
        Domain::EmulatorProfile updated = dialog.updatedEmulator();
        Database::DatabaseManager::instance().saveEmulator(updated);
        Database::DatabaseManager::instance().setSystemDefaultEmulator(sys.id, updated.id);
        statusBar()->showMessage(QString("Emulator profile '%1' saved for system '%2'.").arg(updated.name).arg(sys.name), 3000);
    }
}

void MainWindow::onDeleteSystemClicked(const QUuid& systemId) {
    QUuid targetId = systemId.isNull() ? m_currentSystemId : systemId;
    if (targetId.isNull()) return;

    Domain::System sys = Database::DatabaseManager::instance().getSystem(targetId);
    if (sys.id.isNull()) return;

    auto reply = QMessageBox::question(this, "Confirm System Deletion",
        QString("Are you sure you want to remove system '%1' and all its game records from LudoShelf?\n\nNote: ROM files on your disk will NOT be deleted.").arg(sys.name),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        Database::DatabaseManager::instance().deleteSystem(targetId);
        m_currentSystemId = {};
        loadSystems();
        statusBar()->showMessage(QString("System '%1' removed from library.").arg(sys.name), 3000);
    }
}

void MainWindow::onExportJsonClicked() {
    QString file = QFileDialog::getSaveFileName(this, "Export Library to JSON", "ludoshelf_library.json", "JSON Files (*.json)");
    if (!file.isEmpty()) {
        if (App::LibraryBackupService::exportLibraryToJson(file)) {
            QMessageBox::information(this, "Export Success", "Library successfully exported to JSON.");
        } else {
            QMessageBox::critical(this, "Export Error", "Failed to export library to JSON.");
        }
    }
}

void MainWindow::onImportJsonClicked() {
    QString file = QFileDialog::getOpenFileName(this, "Import Library from JSON", QString(), "JSON Files (*.json)");
    if (!file.isEmpty()) {
        if (App::LibraryBackupService::importLibraryFromJson(file)) {
            loadSystems();
            QMessageBox::information(this, "Import Success", "Library successfully imported from JSON.");
        } else {
            QMessageBox::critical(this, "Import Error", "Failed to import library from JSON.");
        }
    }
}

void MainWindow::onCreateBackupClicked() {
    if (Database::DatabaseManager::instance().createBackup()) {
        QMessageBox::information(this, "Backup Created", "Database backup successfully created in application backup folder.");
    } else {
        QMessageBox::critical(this, "Backup Failed", "Failed to create database backup.");
    }
}

void MainWindow::onDatAuditClicked() {
    if (m_currentSystemId.isNull()) {
        QMessageBox::warning(this, "Select System", "Please select a system first to perform DAT auditing.");
        return;
    }

    DatAuditDialog dialog(m_currentSystemId, this);
    dialog.exec();
}

void MainWindow::onDiagnosticsClicked() {
    DiagnosticsDialog dialog(this);
    dialog.exec();
}

void MainWindow::onEditGameClicked() {
    if (m_selectedGameId.isNull()) return;

    Domain::Game g = Database::DatabaseManager::instance().getGame(m_selectedGameId);
    if (g.id.isNull()) return;

    EditGameDialog dialog(g, this);
    if (dialog.exec() == QDialog::Accepted) {
        Domain::Game updated = dialog.updatedGame();
        Domain::GameFile primaryFile = Database::DatabaseManager::instance().getPrimaryFileForGame(updated.id);
        if (Database::DatabaseManager::instance().saveGame(updated, primaryFile)) {
            loadGamesForSystem(m_currentSystemId);
            onGameSelected(updated.id);
        }
    }
}

void MainWindow::onTestLaunchClicked() {
    if (m_selectedGameId.isNull()) return;

    Domain::Game targetGame = Database::DatabaseManager::instance().getGame(m_selectedGameId);
    Domain::System targetSys = Database::DatabaseManager::instance().getSystem(targetGame.systemId);

    Domain::EmulatorProfile emuProfile;
    if (!targetGame.emulatorOverrideId.isNull()) {
        emuProfile = Database::DatabaseManager::instance().getEmulator(targetGame.emulatorOverrideId);
    }

    if (emuProfile.program.isEmpty()) {
        QUuid defaultEmuId = Database::DatabaseManager::instance().getSystemDefaultEmulator(targetSys.id);
        if (!defaultEmuId.isNull()) {
            emuProfile = Database::DatabaseManager::instance().getEmulator(defaultEmuId);
        }
    }

    if (emuProfile.program.isEmpty()) {
        emuProfile.name = "Default Launcher";
        emuProfile.program = "retroarch";
    }

    Domain::GameFile primaryFile = Database::DatabaseManager::instance().getPrimaryFileForGame(targetGame.id);

    Launch::LaunchCommand cmd = Launch::LaunchService::prepareCommand(targetGame, primaryFile, targetSys, emuProfile);

    TestLaunchDialog dialog(cmd, this);
    connect(&dialog, &TestLaunchDialog::executeLaunchRequested, this, [this, targetGame]() {
        onGameActivated(targetGame.id);
    });
    dialog.exec();
}

void MainWindow::onToggleFavoriteClicked() {
    if (m_selectedGameId.isNull()) return;

    Domain::Game g = Database::DatabaseManager::instance().getGame(m_selectedGameId);
    if (g.id.isNull()) return;

    g.favorite = !g.favorite;
    Domain::GameFile primaryFile = Database::DatabaseManager::instance().getPrimaryFileForGame(g.id);
    if (Database::DatabaseManager::instance().saveGame(g, primaryFile)) {
        loadGamesForSystem(m_currentSystemId);
        onGameSelected(g.id);
    }
}

void MainWindow::onRevealFileClicked() {
    if (m_selectedGameId.isNull()) return;

    Domain::GameFile primaryFile = Database::DatabaseManager::instance().getPrimaryFileForGame(m_selectedGameId);
    if (!primaryFile.path.isEmpty() && QFile::exists(primaryFile.path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(primaryFile.path).absolutePath()));
    } else {
        QMessageBox::warning(this, "File Not Found", "Game file path does not exist on disk.");
    }
}

void MainWindow::onManageCoverArtClicked() {
    if (m_selectedGameId.isNull()) return;
    const QUuid gameId = m_selectedGameId;
    CoverArtDialog dialog(gameId, this);
    dialog.exec();
    loadGamesForSystem(m_currentSystemId);
    onGameSelected(gameId);
}

void MainWindow::onFindCoverArtClicked() {
    if (m_currentSystemId.isNull()) {
        statusBar()->showMessage("Select a system before finding cover art.", 4000);
        return;
    }
    const auto games = Database::DatabaseManager::instance().getGamesForSystem(m_currentSystemId);
    const auto system = Database::DatabaseManager::instance().getSystem(m_currentSystemId);
    QList<QUuid> gameIds;
    for (const auto& game : games) gameIds.append(game.id);
    scheduleEnrichment(gameIds, true, false);
    statusBar()->showMessage(QString("Cover-art lookup queued for %1 games.").arg(games.size()), 5000);
}

void MainWindow::onRefreshRomMetadataClicked(bool force) {
    if (m_currentSystemId.isNull()) { statusBar()->showMessage("Select a system before refreshing ROM metadata.", 4000); return; }
    const auto system = Database::DatabaseManager::instance().getSystem(m_currentSystemId);
    const auto games = Database::DatabaseManager::instance().getGamesForSystem(m_currentSystemId);
    for (const auto& game : games)
        m_romMetadataCoordinator->request(game, Database::DatabaseManager::instance().getPrimaryFileForGame(game.id), system, force, true);
    statusBar()->showMessage(QString("Refreshing ROM metadata for %1 games…").arg(games.size()), 5000);
}

void MainWindow::scheduleEnrichment(const QList<QUuid>& gameIds, bool coverArt, bool metadata) {
    for (const QUuid& gameId : gameIds) {
        if (gameId.isNull()) continue;
        auto pending = m_enrichmentByGame.find(gameId);
        if (pending == m_enrichmentByGame.end()) {
            m_enrichmentQueue.enqueue(gameId);
            pending = m_enrichmentByGame.insert(gameId, {});
        }
        pending->coverArt = pending->coverArt || coverArt;
        pending->metadata = pending->metadata || metadata;
    }
    if (m_enrichmentQueue.isEmpty()) return;
    prepareRetroArchContext();
    scheduleNextEnrichment();
}

void MainWindow::prepareRetroArchContext() {
    if (m_retroArchContextReady || m_retroArchContextWatcher->isRunning()) return;
    m_retroArchContextWatcher->setFuture(QtConcurrent::run([] {
        return Covers::RetroArchCoverDiscovery::buildDiscoveryContext();
    }));
}

void MainWindow::scheduleNextEnrichment() {
    if (!m_retroArchContextReady || m_enrichmentQueue.isEmpty() || m_enrichmentTimerScheduled) return;
    m_enrichmentTimerScheduled = true;
    QTimer::singleShot(0, this, &MainWindow::processNextEnrichment);
}

void MainWindow::processNextEnrichment() {
    m_enrichmentTimerScheduled = false;
    if (m_enrichmentQueue.isEmpty()) return;
    const QUuid gameId = m_enrichmentQueue.dequeue();
    const PendingEnrichment pending = m_enrichmentByGame.take(gameId);
    const Domain::Game game = Database::DatabaseManager::instance().getGame(gameId);
    if (!game.id.isNull()) {
        const Domain::GameFile file = Database::DatabaseManager::instance().getPrimaryFileForGame(gameId);
        const Domain::System system = Database::DatabaseManager::instance().getSystem(game.systemId);
        const bool deferCoverUntilMetadata = pending.coverArt && pending.metadata;
        if (pending.coverArt && !deferCoverUntilMetadata) {
            const auto preferredCover = Database::DatabaseManager::instance().getPreferredCoverAsset(gameId);
            if (preferredCover.providerId.isEmpty())
                Media::PlaceholderGenerator::generateAndStorePlaceholder(gameId, game.title, system.name);
            requestCoverArt(gameId, true, &m_retroArchContext);
        }
        if (pending.metadata) {
            if (deferCoverUntilMetadata) {
                m_metadataAwaitingCover.insert(gameId);
                m_coverAfterMetadata.insert(gameId);
                showEnrichmentProgress(QStringLiteral("Retrieving ROM metadata"),
                                       static_cast<int>(m_coverAfterMetadata.size() - m_metadataAwaitingCover.size()),
                                       static_cast<int>(m_coverAfterMetadata.size()));
            }
            m_romMetadataCoordinator->enrich(game, file, system);
        }
    }
    scheduleNextEnrichment();
}

void MainWindow::finishMetadataBeforeCover(const QUuid& gameId) {
    if (!m_metadataAwaitingCover.remove(gameId)) return;
    showEnrichmentProgress(QStringLiteral("Retrieving ROM metadata"),
                           static_cast<int>(m_coverAfterMetadata.size() - m_metadataAwaitingCover.size()),
                           static_cast<int>(m_coverAfterMetadata.size()));
    tryBeginDeferredCoverAfterMetadata();
}

void MainWindow::tryBeginDeferredCoverAfterMetadata() {
    if (!m_metadataAwaitingCover.isEmpty() || m_coverAfterMetadata.isEmpty() || m_coverDelayScheduled) return;

    // Metadata can publish a useful cached result before its stale refresh,
    // title fallback, or content-hash lookup has finished.  Treat a terminal
    // UI state as progress only; the cover phase may begin solely after the
    // coordinator has no outstanding work and the import queue is drained.
    if (!m_enrichmentQueue.isEmpty() || !m_romMetadataCoordinator->isIdle()) {
        if (!m_metadataBarrierRecheckScheduled) {
            m_metadataBarrierRecheckScheduled = true;
            QTimer::singleShot(100, this, [this] {
                m_metadataBarrierRecheckScheduled = false;
                tryBeginDeferredCoverAfterMetadata();
            });
        }
        return;
    }

    m_coverDelayScheduled = true;
    QTimer::singleShot(750, this, [this] {
        m_coverDelayScheduled = false;
        // A later import or a stale-cache refresh may have joined this batch
        // while the delay was pending.  Re-evaluate the full barrier rather
        // than starting covers from a partial metadata snapshot.
        if (!m_metadataAwaitingCover.isEmpty() || !m_enrichmentQueue.isEmpty() ||
            !m_romMetadataCoordinator->isIdle()) {
            tryBeginDeferredCoverAfterMetadata();
            return;
        }
        const QList<QUuid> gameIds = m_coverAfterMetadata.values();
        m_coverAfterMetadata.clear();
        beginDeferredCoverBatch(gameIds);
    });
}

void MainWindow::beginDeferredCoverBatch(const QList<QUuid>& gameIds) {
    for (const QUuid& gameId : gameIds) {
        if (!gameId.isNull()) m_coverAwaitingCompletion.insert(gameId);
    }
    m_coverBatchTotal = static_cast<int>(m_coverAwaitingCompletion.size());
    if (m_coverBatchTotal == 0) {
        hideEnrichmentProgressSoon();
        return;
    }

    // This second phase must not re-enter the enrichment scheduler: metadata
    // and RetroArch discovery use that scheduler, while cover acquisition has
    // its own asynchronous queue.  Starting it here guarantees the phase
    // transition after the final metadata terminal state.
    showEnrichmentProgress(QStringLiteral("Resolving cover art"), 0, m_coverBatchTotal);
    for (const QUuid& gameId : gameIds) {
        const Domain::Game game = Database::DatabaseManager::instance().getGame(gameId);
        if (game.id.isNull()) {
            completeDeferredCover(gameId);
            continue;
        }
        const Domain::System system = Database::DatabaseManager::instance().getSystem(game.systemId);
        const auto preferredCover = Database::DatabaseManager::instance().getPreferredCoverAsset(gameId);
        if (preferredCover.providerId.isEmpty())
            Media::PlaceholderGenerator::generateAndStorePlaceholder(gameId, game.title, system.name);
        requestCoverArt(gameId, true, &m_retroArchContext);
    }
    statusBar()->showMessage(QString("ROM metadata complete; resolving cover art for %1 games.").arg(m_coverBatchTotal), 5000);
}

void MainWindow::completeDeferredCover(const QUuid& gameId) {
    if (!m_coverAwaitingCompletion.remove(gameId)) return;
    showEnrichmentProgress(QStringLiteral("Downloading cover art"),
                           m_coverBatchTotal - static_cast<int>(m_coverAwaitingCompletion.size()), m_coverBatchTotal);
    if (!m_coverAwaitingCompletion.isEmpty()) return;
    statusBar()->showMessage(QString("Cover-art lookup complete: %1 games processed.").arg(m_coverBatchTotal), 5000);
    hideEnrichmentProgressSoon();
}

void MainWindow::requestCoverArt(const QUuid& gameId, bool automatic,
                                 const Covers::RetroArchDiscoveryContext *retroArchContext) {
    const Domain::Game game = Database::DatabaseManager::instance().getGame(gameId);
    if (game.id.isNull()) return;
    const Domain::System system = Database::DatabaseManager::instance().getSystem(game.systemId);
    const auto existing = Database::DatabaseManager::instance().getPreferredCoverAsset(gameId);
    const bool hasExistingCover = !existing.providerId.isEmpty();
    if ((hasExistingCover && existing.locked) ||
        (automatic && hasExistingCover && existing.kind != Covers::CoverKind::GeneratedPlaceholder)) {
        completeDeferredCover(gameId);
        return;
    }

    const Domain::GameFile file = Database::DatabaseManager::instance().getPrimaryFileForGame(gameId);
    Metadata::RomMetadataRepository metadataRepository;
    const Metadata::CachedMetadata cachedMetadata = metadataRepository.cached(gameId);
    Domain::Game coverGame = game;
    if (coverGame.region.isEmpty() && cachedMetadata.result.kind == Metadata::MetadataResultKind::Match &&
        !cachedMetadata.result.metadata.regions.isEmpty()) {
        coverGame.region = cachedMetadata.result.metadata.regions.first();
    }

    const auto localCandidates = Covers::LocalCoverDiscovery::findCandidates(coverGame, file, system);
    if (!localCandidates.isEmpty()) {
        QFile image(localCandidates.first().downloadUrl.toLocalFile());
        if (image.open(QIODevice::ReadOnly) && !Media::MediaStorageManager::instance().storeCoverCandidate(gameId, image.readAll(), localCandidates.first()).isEmpty()) {
            loadGamesForSystem(m_currentSystemId);
            onGameSelected(gameId);
            completeDeferredCover(gameId);
            statusBar()->showMessage("Local cover art imported.", 4000);
            return;
        }
    }

    Covers::RetroArchPlaylistMatch playlistMatch;
    const auto retroArchCandidates = Covers::RetroArchCoverDiscovery::findLocalCandidates(
        coverGame, file, system, &playlistMatch, {}, {}, retroArchContext);
    for (const auto& candidate : retroArchCandidates) {
        if (candidate.matchConfidence < 0.9) continue;
        QFile image(candidate.downloadUrl.toLocalFile());
        if (image.open(QIODevice::ReadOnly) && !Media::MediaStorageManager::instance().storeCoverCandidate(gameId, image.readAll(), candidate).isEmpty()) {
            loadGamesForSystem(m_currentSystemId);
            onGameSelected(gameId);
            completeDeferredCover(gameId);
            statusBar()->showMessage("Reused cover art from RetroArch.", 4000);
            return;
        }
    }

    QStringList titleCandidates;
    QString collectionName;
    if (cachedMetadata.result.kind == Metadata::MetadataResultKind::Match) {
        const auto& metadata = cachedMetadata.result.metadata;
        if (!metadata.canonicalTitle.isEmpty()) titleCandidates.append(metadata.canonicalTitle);
        collectionName = Covers::LibretroCoverProvider::collectionForMetadataPlatform(metadata.platform);
    }
    titleCandidates.append(QFileInfo(file.path).completeBaseName());
    if (!playlistMatch.label.isEmpty()) titleCandidates.append(playlistMatch.label);
    titleCandidates.append(coverGame.title);
    if (!coverGame.title.contains('(')) {
        if (!coverGame.region.isEmpty()) titleCandidates.append(coverGame.title + " (" + coverGame.region + ")");
        else titleCandidates.append({coverGame.title + " (USA)", coverGame.title + " (World)",
                                     coverGame.title + " (Europe)", coverGame.title + " (Japan)"});
    }
    if (collectionName.isEmpty()) collectionName = playlistMatch.collectionName;
    if (collectionName.isEmpty()) collectionName = Covers::LibretroCoverProvider::collectionForSystem(system);

    QPointer<MainWindow> window(this);
    m_thumbnailCatalog->resolve(collectionName, titleCandidates,
        [window, gameId, coverGame, system, titleCandidates, collectionName](const QStringList& assetPaths) {
            if (!window) return;
            const auto preferredCover = Database::DatabaseManager::instance().getPreferredCoverAsset(gameId);
            if (!preferredCover.providerId.isEmpty() && preferredCover.kind != Covers::CoverKind::GeneratedPlaceholder) {
                window->completeDeferredCover(gameId);
                return;
            }
            QList<Covers::CoverCandidate> candidates = Covers::LibretroCoverProvider::candidatesForCatalogAssets(
                coverGame, system, collectionName, assetPaths);
            if (candidates.isEmpty()) {
                // Catalog retrieval can be unavailable (for example, an offline
                // portable install).  Retain the conservative filename path as
                // a fallback rather than treating that as a hard failure.
                candidates = Covers::LibretroCoverProvider::candidatesFor(coverGame, system, titleCandidates, collectionName);
            }
            QList<Covers::CoverCandidate> safeCandidates;
            for (const auto& candidate : candidates) if (candidate.matchConfidence >= 0.65) safeCandidates.append(candidate);
            if (!safeCandidates.isEmpty()) {
                window->m_coverAcquisitionService->downloadCandidates(gameId, safeCandidates);
                window->statusBar()->showMessage(QString("Searching Libretro for %1…").arg(coverGame.title));
            } else {
                window->completeDeferredCover(gameId);
                window->statusBar()->showMessage("No exact credential-free cover candidate was found.", 5000);
            }
        });
    // Resolution is asynchronous.  Do not leave a stale per-game message in
    // the status bar if a catalog request cannot produce a download job.
    statusBar()->showMessage(QString("Resolving official Libretro cover title for %1…").arg(game.title), 5000);
}

void MainWindow::showGameContextMenu(const QPoint& pos) {
    QWidget *senderWidget = qobject_cast<QWidget*>(sender());
    if (!senderWidget) return;

    QModelIndex idx;
    if (auto *tv = qobject_cast<GamesTableView*>(senderWidget)) {
        idx = tv->indexAt(pos);
    } else if (auto *gv = qobject_cast<GamesGridView*>(senderWidget)) {
        idx = gv->indexAt(pos);
    }

    if (!idx.isValid()) return;

    QUuid gameId = QUuid::fromString(m_gameFilterProxyModel->data(idx, Models::GameTableModel::GameIdRole).toString());
    m_selectedGameId = gameId;

    QMenu menu(this);
    menu.addAction("▶ Play", this, [this, gameId]() { onGameActivated(gameId); });
    menu.addAction("Test Launch Command...", this, &MainWindow::onTestLaunchClicked);
    menu.addSeparator();
    menu.addAction("Edit Game...", this, &MainWindow::onEditGameClicked);
    menu.addAction("Toggle Favorite", this, &MainWindow::onToggleFavoriteClicked);
    menu.addAction("Manage Cover Art...", this, &MainWindow::onManageCoverArtClicked);
    menu.addAction("Fetch Missing Cover Art for Current System", this, &MainWindow::onFindCoverArtClicked);
    menu.addAction("Refresh This ROM Metadata", this, [this, gameId] {
        const auto game = Database::DatabaseManager::instance().getGame(gameId);
        if (!game.id.isNull()) m_romMetadataCoordinator->request(game, Database::DatabaseManager::instance().getPrimaryFileForGame(gameId),
            Database::DatabaseManager::instance().getSystem(game.systemId), true, true);
    });
    menu.addAction("Reveal File in File Manager", this, &MainWindow::onRevealFileClicked);

    menu.exec(senderWidget->mapToGlobal(pos));
}

} // namespace LudoShelf::UI
