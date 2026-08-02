#include "AddSystemWizard.h"
#include "SystemPresets.h"
#include "../app/AppPaths.h"
#include "../database/DatabaseManager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QFileDialog>
#include <QHeaderView>
#include <QMessageBox>
#include <QFile>
#include <QtConcurrentRun>

namespace LudoShelf::UI {

AddSystemWizard::AddSystemWizard(QWidget *parent)
    : QWizard(parent)
{
    setWindowTitle("Add System & Import Games Wizard");
    resize(700, 500);

    addPage(createIdentityPage());
    addPage(createLocationPage());
    addPage(createEmulatorPage());
    addPage(createPreviewPage());

    m_previewScanWatcher = new QFutureWatcher<QList<Scanning::ScanCandidate>>(this);
    connect(m_previewScanWatcher, &QFutureWatcher<QList<Scanning::ScanCandidate>>::finished, this, [this] {
        m_discoveredCandidates = m_previewScanWatcher->result();
        m_previewCountLabel->setText(QString("Discovered %1 games").arg(m_discoveredCandidates.size()));
        m_previewTable->setRowCount(0);
        for (const auto& candidate : m_discoveredCandidates) {
            const int row = m_previewTable->rowCount();
            m_previewTable->insertRow(row);
            m_previewTable->setItem(row, 0, new QTableWidgetItem(candidate.game.title));
            m_previewTable->setItem(row, 1, new QTableWidgetItem(candidate.game.region));
            m_previewTable->setItem(row, 2, new QTableWidgetItem(candidate.file.path));
        }
        button(QWizard::FinishButton)->setEnabled(true);
    });
}

QWizardPage* AddSystemWizard::createIdentityPage() {
    auto *page = new QWizardPage(this);
    page->setTitle("Step 1: System Identity");
    page->setSubTitle("Define the basic information for this gaming system.");

    auto *layout = new QFormLayout(page);

    m_presetCombo = new QComboBox(page);
    for (const SystemPreset& preset : systemPresets())
        m_presetCombo->addItem(preset.displayName, preset.shortName);
    m_presetCombo->addItem("[Custom]", QString());
    m_presetCombo->setCurrentIndex(m_presetCombo->count() - 1);
    layout->addRow("System:", m_presetCombo);

    m_nameEdit = new QLineEdit(page);
    m_nameEdit->setPlaceholderText("e.g. Nintendo Entertainment System");
    layout->addRow("System Display Name*:", m_nameEdit);

    m_shortNameEdit = new QLineEdit(page);
    m_shortNameEdit->setPlaceholderText("e.g. nes");
    layout->addRow("Short Identifier:", m_shortNameEdit);

    m_manufacturerEdit = new QLineEdit(page);
    m_manufacturerEdit->setPlaceholderText("e.g. Nintendo");
    layout->addRow("Manufacturer:", m_manufacturerEdit);

    const auto applyPreset = [this](int index) {
        const bool custom = index < 0 || index >= systemPresets().size();
        m_nameEdit->setEnabled(custom);
        m_shortNameEdit->setEnabled(custom);
        m_manufacturerEdit->setEnabled(custom);
        if (!custom) {
            const SystemPreset& preset = systemPresets().at(index);
            m_nameEdit->setText(preset.displayName);
            m_shortNameEdit->setText(preset.shortName);
            m_manufacturerEdit->setText(preset.manufacturer);
        }
    };
    connect(m_presetCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, applyPreset);

    m_iconList = new QListWidget(page);
    m_iconList->setViewMode(QListView::IconMode);
    m_iconList->setFlow(QListView::LeftToRight);
    m_iconList->setWrapping(true);
    m_iconList->setResizeMode(QListView::Adjust);
    m_iconList->setIconSize(QSize(52, 52));
    m_iconList->setGridSize(QSize(88, 82));
    m_iconList->setFixedHeight(176);
    m_iconList->setSelectionMode(QAbstractItemView::SingleSelection);

    auto *noIcon = new QListWidgetItem("None", m_iconList);
    noIcon->setData(Qt::UserRole, QString());
    const QDir iconsDirectory(App::AppPaths::controllerIconsRoot());
    const QFileInfoList iconFiles = iconsDirectory.entryInfoList(
        {"*.png", "*.svg", "*.jpg", "*.jpeg", "*.webp"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& iconFile : iconFiles) {
        auto *item = new QListWidgetItem(QIcon(iconFile.absoluteFilePath()), iconFile.completeBaseName(), m_iconList);
        item->setData(Qt::UserRole,
            QDir(QCoreApplication::applicationDirPath()).relativeFilePath(iconFile.absoluteFilePath()));
        item->setToolTip(iconFile.fileName());
    }
    m_iconList->setCurrentItem(noIcon);
    layout->addRow("Controller Icon:", m_iconList);

    connect(m_nameEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (m_presetCombo->currentIndex() < systemPresets().size()) return;
        if (m_shortNameEdit->text().isEmpty() || m_shortNameEdit->text() == text.left(text.length()-1).toLower().remove(' ')) {
            m_shortNameEdit->setText(text.toLower().remove(' '));
        }
    });

    return page;
}

QWizardPage* AddSystemWizard::createLocationPage() {
    auto *page = new QWizardPage(this);
    page->setTitle("Step 2: Game Locations & Extensions");
    page->setSubTitle("Select directory containing your game files/ROMs.");

    auto *layout = new QFormLayout(page);

    auto *dirLayout = new QHBoxLayout();
    m_romDirEdit = new QLineEdit(page);
    dirLayout->addWidget(m_romDirEdit);

    auto *browseBtn = new QPushButton("Browse...", page);
    dirLayout->addWidget(browseBtn);
    layout->addRow("ROM Directory*:", dirLayout);

    connect(browseBtn, &QPushButton::clicked, this, [this, page]() {
        QString dir = QFileDialog::getExistingDirectory(page, "Select ROM Directory");
        if (!dir.isEmpty()) {
            m_romDirEdit->setText(dir);
        }
    });

    m_extensionsEdit = new QLineEdit(".zip, .7z", page);
    layout->addRow("Allowed Extensions:", m_extensionsEdit);

    m_recursiveCheck = new QCheckBox("Scan subdirectories recursively", page);
    m_recursiveCheck->setChecked(true);
    layout->addRow("", m_recursiveCheck);

    return page;
}

QWizardPage* AddSystemWizard::createEmulatorPage() {
    auto *page = new QWizardPage(this);
    page->setTitle("Step 3: Emulator Configuration");
    page->setSubTitle("Configure command-line executable and argument templates.");

    auto *layout = new QFormLayout(page);

    m_emulatorNameEdit = new QLineEdit("RetroArch", page);
    layout->addRow("Profile Name:", m_emulatorNameEdit);

    auto *progLayout = new QHBoxLayout();
    m_emulatorPathEdit = new QLineEdit("retroarch", page);
    progLayout->addWidget(m_emulatorPathEdit);

    auto *browseBtn = new QPushButton("Browse...", page);
    progLayout->addWidget(browseBtn);
    layout->addRow("Program Executable*:", progLayout);

    connect(browseBtn, &QPushButton::clicked, this, [this, page]() {
        QString file = QFileDialog::getOpenFileName(page, "Select Emulator Executable");
        if (!file.isEmpty()) {
            m_emulatorPathEdit->setText(file);
        }
    });

    m_argumentsEdit = new QPlainTextEdit("{game.path}", page);
    m_argumentsEdit->setPlaceholderText("One ordered command fragment per line, e.g. -L /path/to/core, then {game.path}");
    m_argumentsEdit->setFixedHeight(78);
    layout->addRow("Ordered Arguments:\n(command fragment per line)", m_argumentsEdit);

    return page;
}

QWizardPage* AddSystemWizard::createPreviewPage() {
    auto *page = new QWizardPage(this);
    page->setTitle("Step 4: Scan Preview");
    page->setSubTitle("Review discovered game files before committing.");

    auto *layout = new QVBoxLayout(page);

    m_previewCountLabel = new QLabel("Discovered 0 games", page);
    layout->addWidget(m_previewCountLabel);

    m_previewTable = new QTableWidget(0, 3, page);
    m_previewTable->setHorizontalHeaderLabels({"Title", "Region", "File Path"});
    m_previewTable->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_previewTable);

    connect(this, &QWizard::currentIdChanged, this, [this](int id) {
        if (id == 3) { // Preview page index
            QStringList exts = m_extensionsEdit->text().split(',', Qt::SkipEmptyParts);
            const QUuid systemId = m_system.id;
            const QString path = m_romDirEdit->text();
            const bool recursive = m_recursiveCheck->isChecked();
            m_previewCountLabel->setText("Scanning ROM folder in the background…");
            m_previewTable->setRowCount(0);
            button(QWizard::FinishButton)->setEnabled(false);
            m_previewScanWatcher->setFuture(QtConcurrent::run([systemId, path, exts, recursive] {
                Scanning::DirectoryScanner scanner;
                return scanner.scanDirectory(systemId, path, exts, recursive);
            }));
        }
    });

    return page;
}

void AddSystemWizard::accept() {
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "System name cannot be empty.");
        return;
    }

    m_system.name = m_nameEdit->text().trimmed();
    m_system.shortName = m_shortNameEdit->text().trimmed();
    m_system.manufacturer = m_manufacturerEdit->text().trimmed();
    m_system.iconPath = m_iconList->currentItem()
        ? m_iconList->currentItem()->data(Qt::UserRole).toString() : QString();
    // 1. Save System
    if (!Database::DatabaseManager::instance().saveSystem(m_system)) {
        QMessageBox::critical(this, "Error", "Failed to save system to database.");
        return;
    }

    // 2. Save Scan Root
    Database::ScanRoot root;
    root.systemId = m_system.id;
    root.path = m_romDirEdit->text().trimmed();
    root.recursive = m_recursiveCheck->isChecked();
    root.includeExtensions = m_extensionsEdit->text().split(',', Qt::SkipEmptyParts);
    Database::DatabaseManager::instance().saveScanRoot(root);

    // 3. Save Emulator Profile
    if (!m_emulatorPathEdit->text().trimmed().isEmpty()) {
        Domain::EmulatorProfile emu;
        emu.name = m_emulatorNameEdit->text().trimmed();
        emu.program = m_emulatorPathEdit->text().trimmed();

        const QStringList argumentLines = m_argumentsEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
        for (const QString& line : argumentLines) {
            const QString templateString = line.trimmed();
            if (templateString.isEmpty()) continue;
            Domain::ArgumentTemplate arg;
            arg.position = static_cast<int>(emu.arguments.size());
            arg.templateString = templateString;
            emu.arguments.append(arg);
        }

        Database::DatabaseManager::instance().saveEmulator(emu);
        Database::DatabaseManager::instance().setSystemDefaultEmulator(m_system.id, emu.id);
    }

    // 4. Save Discovered Games Batch
    QList<QPair<Domain::Game, Domain::GameFile>> batch;
    for (auto candidate : m_discoveredCandidates) {
        candidate.game.systemId = m_system.id;
        batch.append({candidate.game, candidate.file});
    }

    if (!batch.isEmpty()) {
        if (!Database::DatabaseManager::instance().saveGamesBatch(batch)) {
            QMessageBox::critical(this, "Error", "Failed to save discovered games.");
            return;
        }
        m_importedGameIds.clear();
        for (const auto& candidate : m_discoveredCandidates) m_importedGameIds.append(candidate.game.id);
    }

    QWizard::accept();
}


} // namespace LudoShelf::UI
