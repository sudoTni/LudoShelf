#include "DatAuditDialog.h"
#include "../database/DatabaseManager.h"
#include "../dat/HashService.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QPointer>
#include <QMetaObject>
#include <QtConcurrentRun>

namespace LudoShelf::UI {

DatAuditDialog::DatAuditDialog(const QUuid& systemId, QWidget *parent)
    : QDialog(parent)
    , m_systemId(systemId)
{
    setWindowTitle("DAT Audit & Collection Verification");
    resize(750, 500);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    auto *datLayout = new QHBoxLayout();
    m_datPathEdit = new QLineEdit(this);
    m_datPathEdit->setPlaceholderText("Select Logiqx XML or ClrMamePro DAT file...");
    datLayout->addWidget(m_datPathEdit);

    auto *browseBtn = new QPushButton("Browse...", this);
    datLayout->addWidget(browseBtn);

    form->addRow("DAT File:", datLayout);
    layout->addLayout(form);

    m_auditBtn = new QPushButton("▶ Run Audit", this);
    m_auditBtn->setStyleSheet("padding: 6px 16px; font-weight: bold; background-color: #2b78e4; color: white;");
    layout->addWidget(m_auditBtn);

    m_statusLabel = new QLabel("Select a DAT file to begin audit.", this);
    layout->addWidget(m_statusLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    layout->addWidget(m_progressBar);

    m_resultsTable = new QTableWidget(0, 4, this);
    m_resultsTable->setHorizontalHeaderLabels({"Game Title", "Calculated Hash", "DAT Hash", "Audit Result"});
    m_resultsTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_resultsTable);

    auto *closeBtn = new QPushButton("Close", this);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);

    connect(browseBtn, &QPushButton::clicked, this, &DatAuditDialog::onBrowseDatClicked);
    connect(m_auditBtn, &QPushButton::clicked, this, &DatAuditDialog::onRunAuditClicked);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    m_auditWatcher = new QFutureWatcher<AuditRun>(this);
    connect(m_auditWatcher, &QFutureWatcher<AuditRun>::finished, this, [this] {
        const AuditRun run = m_auditWatcher->result();
        m_auditBtn->setText("▶ Run Audit");
        m_datPathEdit->setEnabled(true);
        m_progressBar->setVisible(false);
        if (run.cancelled) {
            m_statusLabel->setText("Audit cancelled. No collection changes were saved.");
            return;
        }
        if (!run.dat.success) {
            QMessageBox::critical(this, "DAT Error", "Failed to parse DAT file: " + run.dat.errorMessage);
            return;
        }

        Database::DatSource source;
        source.systemId = m_systemId;
        source.name = run.dat.header.name;
        source.version = run.dat.header.version;
        source.author = run.dat.header.author;
        source.category = run.dat.header.category;
        source.filePath = m_datPathEdit->text().trimmed();
        if (!Database::DatabaseManager::instance().saveDatSource(source, run.dat.entries)) {
            QMessageBox::critical(this, "DAT Error", "The parsed DAT could not be saved to the library.");
            return;
        }

        m_resultsTable->setRowCount(0);
        int verifiedCount = 0;
        QSqlDatabase db = Database::DatabaseManager::instance().connection();
        db.transaction();
        for (const AuditItem& item : run.items) {
            const int row = m_resultsTable->rowCount();
            m_resultsTable->insertRow(row);
            m_resultsTable->setItem(row, 0, new QTableWidgetItem(item.title));
            if (item.missing) {
                m_resultsTable->setItem(row, 1, new QTableWidgetItem("N/A"));
                m_resultsTable->setItem(row, 2, new QTableWidgetItem("N/A"));
                m_resultsTable->setItem(row, 3, new QTableWidgetItem("Missing File"));
                continue;
            }
            Database::DatEntry match;
            const bool found = item.hashes.success && Database::DatabaseManager::instance().matchDatEntry(m_systemId, item.hashes.crc32, item.hashes.sha1, match);
            Database::DatabaseManager::instance().updateFileHashes(item.file.id, item.hashes.crc32, item.hashes.md5, item.hashes.sha1,
                                                                   found ? match.id : QUuid{});
            m_resultsTable->setItem(row, 1, new QTableWidgetItem(item.hashes.crc32.isEmpty() ? item.hashes.sha1 : item.hashes.crc32));
            if (found) {
                ++verifiedCount;
                m_resultsTable->setItem(row, 2, new QTableWidgetItem(match.crc32.isEmpty() ? match.sha1 : match.crc32));
                m_resultsTable->setItem(row, 3, new QTableWidgetItem("Verified"));
            } else {
                m_resultsTable->setItem(row, 2, new QTableWidgetItem("-"));
                m_resultsTable->setItem(row, 3, new QTableWidgetItem("Unverified / No Match"));
            }
        }
        db.commit();
        m_statusLabel->setText(QString("Audit complete. Verified %1 of %2 games.").arg(verifiedCount).arg(run.items.size()));
    });
}

void DatAuditDialog::onBrowseDatClicked() {
    QString file = QFileDialog::getOpenFileName(this, "Select DAT File", QString(), "DAT Files (*.xml *.dat)");
    if (!file.isEmpty()) {
        m_datPathEdit->setText(file);
    }
}

void DatAuditDialog::onRunAuditClicked() {
    if (m_auditWatcher->isRunning()) {
        m_cancelRequested->store(true);
        m_auditBtn->setEnabled(false);
        m_statusLabel->setText("Cancelling audit after the current file…");
        return;
    }
    QString datPath = m_datPathEdit->text().trimmed();
    if (datPath.isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Please select a DAT file.");
        return;
    }

    auto games = Database::DatabaseManager::instance().getGamesForSystem(m_systemId);
    m_resultsTable->setRowCount(0);
    m_progressBar->setVisible(true);
    m_progressBar->setRange(0, static_cast<int>(games.size()));
    m_progressBar->setValue(0);
    m_auditBtn->setText("Cancel Audit");
    m_auditBtn->setEnabled(true);
    m_datPathEdit->setEnabled(false);
    m_statusLabel->setText(QString("Hashing %1 game files…").arg(games.size()));
    QList<QPair<QString, Domain::GameFile>> files;
    for (const Domain::Game& game : games)
        files.append({game.title, Database::DatabaseManager::instance().getPrimaryFileForGame(game.id)});
    m_cancelRequested = std::make_shared<std::atomic_bool>(false);
    const QPointer<DatAuditDialog> dialog(this);
    const auto cancelled = m_cancelRequested;
    m_auditWatcher->setFuture(QtConcurrent::run([datPath, files, dialog, cancelled] {
        AuditRun run;
        run.dat = Dat::DatParser::parseDatFile(datPath);
        if (!run.dat.success || cancelled->load()) { run.cancelled = cancelled->load(); return run; }
        run.items.reserve(files.size());
        for (int i = 0; i < files.size(); ++i) {
            if (cancelled->load()) { run.cancelled = true; break; }
            AuditItem item;
            item.title = files.at(i).first;
            item.file = files.at(i).second;
            item.missing = item.file.path.isEmpty() || !QFile::exists(item.file.path);
            if (!item.missing) item.hashes = Dat::HashService::calculateHashes(item.file.path);
            run.items.append(item);
            if (dialog) QMetaObject::invokeMethod(dialog, [dialog, i, total = files.size()] {
                if (dialog) dialog->m_progressBar->setValue(i + 1);
                if (dialog) dialog->m_statusLabel->setText(QString("Hashing game %1 of %2…").arg(i + 1).arg(total));
            }, Qt::QueuedConnection);
        }
        return run;
    }));
}

} // namespace LudoShelf::UI
