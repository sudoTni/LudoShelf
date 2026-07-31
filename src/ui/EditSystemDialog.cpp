#include "EditSystemDialog.h"
#include "SystemPresets.h"

#include "../app/AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>

namespace LudoShelf::UI {

EditSystemDialog::EditSystemDialog(const Domain::System& system, QWidget *parent)
    : QDialog(parent)
    , m_system(system)
{
    setWindowTitle("Edit System Properties");
    resize(560, 570);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_presetCombo = new QComboBox(this);
    for (const SystemPreset& preset : systemPresets())
        m_presetCombo->addItem(preset.displayName, preset.shortName);
    m_presetCombo->addItem("[Custom]", QString());
    form->addRow("System:", m_presetCombo);

    m_nameEdit = new QLineEdit(system.name, this);
    form->addRow("Display Name*:", m_nameEdit);

    m_shortNameEdit = new QLineEdit(system.shortName, this);
    form->addRow("Short Name:", m_shortNameEdit);

    m_manufacturerEdit = new QLineEdit(system.manufacturer, this);
    form->addRow("Manufacturer:", m_manufacturerEdit);

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
    const int existingPreset = matchingSystemPreset(system.name, system.shortName);
    if (existingPreset >= 0) {
        m_presetCombo->setCurrentIndex(existingPreset);
        applyPreset(existingPreset);
    } else {
        m_presetCombo->setCurrentIndex(m_presetCombo->count() - 1);
    }

    m_iconList = new QListWidget(this);
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
    m_iconList->setCurrentItem(noIcon);

    const QDir executableDirectory(QCoreApplication::applicationDirPath());
    const QDir iconsDirectory(App::AppPaths::controllerIconsRoot());
    const QFileInfoList iconFiles = iconsDirectory.entryInfoList(
        {"*.png", "*.svg", "*.jpg", "*.jpeg", "*.webp"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& iconFile : iconFiles) {
        const QString relativePath = executableDirectory.relativeFilePath(iconFile.absoluteFilePath());
        auto *item = new QListWidgetItem(QIcon(iconFile.absoluteFilePath()), iconFile.completeBaseName(), m_iconList);
        item->setData(Qt::UserRole, relativePath);
        item->setToolTip(iconFile.fileName());
        const QFileInfo storedIcon(system.iconPath);
        const bool matchesCurrent = system.iconPath == relativePath ||
            (!system.iconPath.isEmpty() && storedIcon.isAbsolute() &&
             storedIcon.absoluteFilePath() == iconFile.absoluteFilePath());
        if (matchesCurrent) m_iconList->setCurrentItem(item);
    }
    form->addRow("Controller Icon:", m_iconList);

    m_releaseYearSpin = new QSpinBox(this);
    m_releaseYearSpin->setRange(1970, 2030);
    m_releaseYearSpin->setValue(system.releaseYear > 0 ? system.releaseYear : 1990);
    form->addRow("Release Year:", m_releaseYearSpin);

    const auto roots = Database::DatabaseManager::instance().getScanRoots(system.id);
    if (!roots.isEmpty()) {
        m_scanRoot = roots.first();
        m_hadScanRoot = true;
    }
    m_scanRoot.systemId = system.id;

    auto *romDirectoryLayout = new QHBoxLayout();
    m_romDirectoryEdit = new QLineEdit(m_scanRoot.path, this);
    romDirectoryLayout->addWidget(m_romDirectoryEdit);
    auto *browseRomDirectory = new QPushButton("Browse…", this);
    romDirectoryLayout->addWidget(browseRomDirectory);
    form->addRow("ROM Folder:", romDirectoryLayout);
    connect(browseRomDirectory, &QPushButton::clicked, this, [this]() {
        const QString directory = QFileDialog::getExistingDirectory(this, "Select ROM Folder", m_romDirectoryEdit->text());
        if (!directory.isEmpty()) m_romDirectoryEdit->setText(directory);
    });

    const QString extensions = m_scanRoot.includeExtensions.isEmpty()
        ? QStringLiteral(".zip, .7z") : m_scanRoot.includeExtensions.join(", ");
    m_extensionsEdit = new QLineEdit(extensions, this);
    m_extensionsEdit->setPlaceholderText("e.g. .cue, .chd, .gdi, .iso");
    form->addRow("Allowed Extensions:", m_extensionsEdit);

    m_excludedExtensionsEdit = new QLineEdit(m_scanRoot.excludeExtensions.join(", "), this);
    m_excludedExtensionsEdit->setPlaceholderText("e.g. .txt, .nfo");
    form->addRow("Excluded Extensions:", m_excludedExtensionsEdit);

    m_excludedPatternsEdit = new QLineEdit(m_scanRoot.excludePatterns.join(", "), this);
    m_excludedPatternsEdit->setPlaceholderText("e.g. *[BIOS]*, */saves/*");
    form->addRow("Excluded File Patterns:", m_excludedPatternsEdit);

    m_recursiveCheck = new QCheckBox("Scan subfolders", this);
    m_recursiveCheck->setChecked(m_scanRoot.recursive);
    form->addRow("", m_recursiveCheck);

    m_followSymlinksCheck = new QCheckBox("Follow symbolic links", this);
    m_followSymlinksCheck->setChecked(m_scanRoot.followSymlinks);
    form->addRow("", m_followSymlinksCheck);

    m_watchChangesCheck = new QCheckBox("Rescan automatically when the folder changes", this);
    m_watchChangesCheck->setChecked(m_scanRoot.watchChanges);
    form->addRow("", m_watchChangesCheck);

    m_notesText = new QTextEdit(system.notes, this);
    m_notesText->setMaximumHeight(100);
    form->addRow("Notes:", m_notesText);

    layout->addLayout(form);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto *saveBtn = new QPushButton("Save Changes", this);
    saveBtn->setStyleSheet("padding: 6px 14px; font-weight: bold; background-color: #2b78e4; color: white;");
    btnLayout->addWidget(saveBtn);

    auto *cancelBtn = new QPushButton("Cancel", this);
    btnLayout->addWidget(cancelBtn);

    layout->addLayout(btnLayout);

    connect(saveBtn, &QPushButton::clicked, this, &EditSystemDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void EditSystemDialog::accept() {
    if (m_nameEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "System display name cannot be empty.");
        return;
    }

    m_system.name = m_nameEdit->text().trimmed();
    m_system.shortName = m_shortNameEdit->text().trimmed();
    m_system.manufacturer = m_manufacturerEdit->text().trimmed();
    m_system.iconPath = m_iconList->currentItem()
        ? m_iconList->currentItem()->data(Qt::UserRole).toString() : QString();
    m_system.releaseYear = m_releaseYearSpin->value();
    m_system.notes = m_notesText->toPlainText().trimmed();
    m_system.updatedAt = QDateTime::currentDateTimeUtc();
    m_scanRoot.path = m_romDirectoryEdit->text().trimmed();
    m_scanRoot.recursive = m_recursiveCheck->isChecked();
    m_scanRoot.includeExtensions = m_extensionsEdit->text().split(',', Qt::SkipEmptyParts);
    for (QString& extension : m_scanRoot.includeExtensions) extension = extension.trimmed();
    m_scanRoot.excludeExtensions = m_excludedExtensionsEdit->text().split(',', Qt::SkipEmptyParts);
    for (QString& extension : m_scanRoot.excludeExtensions) extension = extension.trimmed();
    m_scanRoot.excludePatterns = m_excludedPatternsEdit->text().split(',', Qt::SkipEmptyParts);
    for (QString& pattern : m_scanRoot.excludePatterns) pattern = pattern.trimmed();
    m_scanRoot.followSymlinks = m_followSymlinksCheck->isChecked();
    m_scanRoot.watchChanges = m_watchChangesCheck->isChecked();

    QDialog::accept();
}

} // namespace LudoShelf::UI
