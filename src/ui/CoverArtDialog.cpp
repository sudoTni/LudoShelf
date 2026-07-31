#include "CoverArtDialog.h"

#include "../covers/CoverTypes.h"
#include "../covers/CoverAcquisitionService.h"
#include "../covers/LibretroCoverProvider.h"
#include "../database/DatabaseManager.h"
#include "../media/MediaStorageManager.h"

#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace LudoShelf::UI {

CoverArtDialog::CoverArtDialog(const QUuid& gameId, QWidget *parent)
    : QDialog(parent), m_gameId(gameId) {
    setWindowTitle("Cover Art");
    resize(780, 420);
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("Covers retain their provider, match method, and rights status. Selecting a cover locks it against automatic replacement.", this));

    m_assetsTable = new QTableWidget(0, 6, this);
    m_assetsTable->setHorizontalHeaderLabels({"Preferred", "Provider", "Kind", "Region", "Match", "Rights"});
    m_assetsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_assetsTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_assetsTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_assetsTable);

    auto *actions = new QHBoxLayout();
    auto *importButton = new QPushButton("Import Cover…", this);
    auto *libretroButton = new QPushButton("Find Libretro Cover", this);
    auto *useButton = new QPushButton("Use Selected Cover", this);
    auto *closeButton = new QPushButton("Close", this);
    actions->addWidget(importButton); actions->addWidget(libretroButton); actions->addWidget(useButton); actions->addStretch(); actions->addWidget(closeButton);
    layout->addLayout(actions);
    connect(importButton, &QPushButton::clicked, this, &CoverArtDialog::importUserCover);
    connect(useButton, &QPushButton::clicked, this, &CoverArtDialog::useSelectedCover);
    connect(libretroButton, &QPushButton::clicked, this, &CoverArtDialog::findLibretroCover);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    reloadAssets();
    m_acquisition = new Covers::CoverAcquisitionService(this);
    connect(m_acquisition, &Covers::CoverAcquisitionService::coverDownloaded, this, [this](const QUuid&, const QString&) { reloadAssets(); });
    connect(m_acquisition, &Covers::CoverAcquisitionService::coverFailed, this, [this](const QUuid&, const QString& reason) {
        QMessageBox::information(this, "No Cover Downloaded", reason);
    });
}

void CoverArtDialog::reloadAssets() {
    const auto assets = Database::DatabaseManager::instance().getCoverAssetsForGame(m_gameId);
    m_assetsTable->setRowCount(0);
    for (const auto& asset : assets) {
        const int row = m_assetsTable->rowCount();
        m_assetsTable->insertRow(row);
        auto *preferred = new QTableWidgetItem(asset.preferred ? "✓" : "");
        preferred->setData(Qt::UserRole, asset.id.toString(QUuid::WithBraces));
        m_assetsTable->setItem(row, 0, preferred);
        m_assetsTable->setItem(row, 1, new QTableWidgetItem(asset.providerId));
        m_assetsTable->setItem(row, 2, new QTableWidgetItem(Covers::coverKindToString(asset.kind)));
        m_assetsTable->setItem(row, 3, new QTableWidgetItem(asset.region.isEmpty() ? "—" : asset.region));
        m_assetsTable->setItem(row, 4, new QTableWidgetItem(asset.matchMethod));
        m_assetsTable->setItem(row, 5, new QTableWidgetItem(asset.rightsStatus));
    }
}

void CoverArtDialog::importUserCover() {
    const QString path = QFileDialog::getOpenFileName(this, "Choose Cover Art", QString(), "Images (*.png *.jpg *.jpeg *.webp *.avif *.bmp)");
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || Media::MediaStorageManager::instance().storeOriginalImage(
            m_gameId, file.readAll(), "box-front", "", "local-user").isEmpty()) {
        QMessageBox::critical(this, "Cover Import Failed", "The file is not a supported, safe cover image.");
        return;
    }
    reloadAssets();
}

void CoverArtDialog::useSelectedCover() {
    const int row = m_assetsTable->currentRow();
    if (row < 0) return;
    const QUuid assetId = QUuid::fromString(m_assetsTable->item(row, 0)->data(Qt::UserRole).toString());
    if (Database::DatabaseManager::instance().setPreferredCoverAsset(m_gameId, assetId)) reloadAssets();
}

void CoverArtDialog::findLibretroCover() {
    const auto game = Database::DatabaseManager::instance().getGame(m_gameId);
    const auto system = Database::DatabaseManager::instance().getSystem(game.systemId);
    const auto candidates = Covers::LibretroCoverProvider::candidatesFor(game, system);
    for (const auto& candidate : candidates) {
        if (candidate.matchConfidence >= 0.9) {
            m_acquisition->download(m_gameId, candidate);
            return;
        }
    }
    QMessageBox::information(this, "No Exact Candidate", "This game has no safe exact Libretro title candidate.");
}

} // namespace LudoShelf::UI
