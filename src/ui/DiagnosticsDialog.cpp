#include "DiagnosticsDialog.h"
#include "../database/DatabaseManager.h"
#include "../app/AppPaths.h"
#include "../metadata/LibretroDatabaseProvider.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QClipboard>
#include <QSysInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QDirIterator>
#include <QFileInfo>


namespace LudoShelf::UI {

DiagnosticsDialog::DiagnosticsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("LudoShelf System Diagnostics");
    resize(650, 500);

    auto *layout = new QVBoxLayout(this);

    m_outputText = new QTextEdit(this);
    m_outputText->setReadOnly(true);
    m_outputText->setStyleSheet("font-family: monospace; background-color: #1e1e1e; color: #d4d4d4;");
    layout->addWidget(m_outputText);

    auto *btnLayout = new QHBoxLayout();
    m_copyBtn = new QPushButton("Copy Diagnostics Bundle", this);
    btnLayout->addWidget(m_copyBtn);

    btnLayout->addStretch();

    auto *closeBtn = new QPushButton("Close", this);
    btnLayout->addWidget(closeBtn);

    layout->addLayout(btnLayout);

    connect(m_copyBtn, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_outputText->toPlainText());
    });

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);

    populateDiagnostics();
}

void DiagnosticsDialog::populateDiagnostics() {
    QString report;
    report += "=== LudoShelf System Diagnostics ===\n";
    report += QString("Application Version: 0.2.0\n");
    report += QString("Qt Version: %1\n").arg(QT_VERSION_STR);
    report += QString("Operating System: %1 (%2)\n")
        .arg(QSysInfo::prettyProductName())
        .arg(QSysInfo::currentCpuArchitecture());
    report += QString("Kernel: %1 %2\n")
        .arg(QSysInfo::kernelType())
        .arg(QSysInfo::kernelVersion());

    QString sessionType = qgetenv("XDG_SESSION_TYPE");
    if (sessionType.isEmpty()) sessionType = "Unknown";
    report += QString("Desktop Session Type: %1\n\n").arg(sessionType);

    report += "=== Database Status ===\n";
    QString dbPath = Database::DatabaseManager::instance().databasePath();
    report += QString("Database Path: %1\n").arg(dbPath);
    report += QString("Database Size: %1 bytes\n").arg(QFileInfo(dbPath).size());

    QString dbIntegrity;
    bool dbOk = Database::DatabaseManager::instance().checkIntegrity(dbIntegrity);
    report += QString("Database Integrity Check: %1 (%2)\n\n").arg(dbOk ? "PASSED" : "FAILED").arg(dbIntegrity);

    report += "=== Storage and Metadata Health ===\n";
    qint64 mediaBytes = 0;
    int mediaFiles = 0;
    QDirIterator mediaIterator(App::AppPaths::mediaRoot(), QDir::Files, QDirIterator::Subdirectories);
    while (mediaIterator.hasNext()) {
        mediaIterator.next();
        mediaBytes += mediaIterator.fileInfo().size();
        ++mediaFiles;
    }
    report += QString("Managed Media: %1 files, %2 bytes\n").arg(mediaFiles).arg(mediaBytes);
    const Metadata::LibretroDatabaseProvider provider;
    report += QString("Libretro Database: %1 (%2)\n")
        .arg(provider.isAvailable() ? "Available" : "Unavailable", provider.databaseRoot());
    report += "Diagnostics may include local paths. Redact before sharing publicly.\n\n";

    report += "=== Configured Systems ===\n";
    auto systems = Database::DatabaseManager::instance().getSystems();
    report += QString("Total Systems: %1\n").arg(systems.size());
    for (const auto& sys : systems) {
        report += QString("  - %1 (%2 games)\n").arg(sys.name).arg(sys.gameCount);
    }
    report += "\n";

    report += "=== Configured Emulators ===\n";
    auto emus = Database::DatabaseManager::instance().getEmulators();
    report += QString("Total Emulator Profiles: %1\n").arg(emus.size());
    for (const auto& emu : emus) {
        bool executableFound = !QStandardPaths::findExecutable(emu.program).isEmpty();
        report += QString("  - Profile '%1' [Executable: %2] -> %3\n")
            .arg(emu.name)
            .arg(emu.program)
            .arg(executableFound ? "Found in PATH" : "NOT FOUND");
    }

    m_outputText->setText(report);
}

} // namespace LudoShelf::UI
