#include "EditEmulatorDialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>

#include <algorithm>

namespace LudoShelf::UI {

EditEmulatorDialog::EditEmulatorDialog(const Domain::EmulatorProfile& emulator, QWidget *parent)
    : QDialog(parent)
    , m_emulator(emulator)
{
    setWindowTitle("Edit Emulator Launch Profile");
    resize(580, 520);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_nameEdit = new QLineEdit(emulator.name.isEmpty() ? "Default Emulator" : emulator.name, this);
    form->addRow("Profile Name*:", m_nameEdit);

    m_launchTypeCombo = new QComboBox(this);
    m_launchTypeCombo->addItems({"Native Binary", "Flatpak Package", "AppImage", "Wine Wrapper", "Custom Script"});
    m_launchTypeCombo->setCurrentIndex(static_cast<int>(emulator.launchType));
    form->addRow("Launch Type:", m_launchTypeCombo);

    m_hidePolicyCombo = new QComboBox(this);
    m_hidePolicyCombo->addItems({"Keep LudoShelf Visible", "Minimize While Playing", "Hide While Playing"});
    m_hidePolicyCombo->setCurrentIndex(static_cast<int>(emulator.hidePolicy));
    form->addRow("While Running:", m_hidePolicyCombo);

    auto *progLayout = new QHBoxLayout();
    m_programEdit = new QLineEdit(emulator.program, this);
    m_programEdit->setPlaceholderText("e.g. /usr/bin/retroarch or pcsx2-qt");
    progLayout->addWidget(m_programEdit);

    auto *browseProgBtn = new QPushButton("Browse...", this);
    progLayout->addWidget(browseProgBtn);
    form->addRow("Executable Path*:", progLayout);

    connect(browseProgBtn, &QPushButton::clicked, this, [this]() {
        QString file = QFileDialog::getOpenFileName(this, "Select Emulator Executable");
        if (!file.isEmpty()) {
            m_programEdit->setText(file);
        }
    });

    auto *workLayout = new QHBoxLayout();
    m_workingDirEdit = new QLineEdit(emulator.workingDirectory, this);
    m_workingDirEdit->setPlaceholderText("Optional working directory");
    workLayout->addWidget(m_workingDirEdit);

    auto *browseWorkBtn = new QPushButton("Browse...", this);
    workLayout->addWidget(browseWorkBtn);
    form->addRow("Working Directory:", workLayout);

    connect(browseWorkBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Working Directory");
        if (!dir.isEmpty()) {
            m_workingDirEdit->setText(dir);
        }
    });

#ifdef Q_OS_WIN
    m_shellModeCheck = new QCheckBox("Execute through shell (cmd.exe /C)", this);
#else
    m_shellModeCheck = new QCheckBox("Execute through shell (/bin/sh -c)", this);
#endif
    m_shellModeCheck->setChecked(emulator.shellMode);
    form->addRow("", m_shellModeCheck);

    m_detachCheck = new QCheckBox("Detach process (outlive frontend)", this);
    m_detachCheck->setChecked(emulator.detach);
    form->addRow("", m_detachCheck);

    m_captureOutputCheck = new QCheckBox("Capture process stdout and stderr", this);
    m_captureOutputCheck->setChecked(emulator.captureOutput);
    form->addRow("", m_captureOutputCheck);

    layout->addLayout(form);

    layout->addWidget(new QLabel("Ordered Argument Templates", this));
    auto *argumentsHelp = new QLabel("One ordered command fragment per line. A line such as “-L /path/to/core” becomes two arguments; quote literal text that must remain one argument. Placeholders include {game.path}, {game.title}, {game.region}, and {system.short_name}. Prefix a line with ? to make it optional when it resolves to an empty value.", this);
    argumentsHelp->setWordWrap(true);
    argumentsHelp->setStyleSheet("color: #aaa;");
    layout->addWidget(argumentsHelp);
    m_argumentsEdit = new QPlainTextEdit(this);
    m_argumentsEdit->setPlaceholderText("-L /path/to/core\n{game.path}");
    QList<Domain::ArgumentTemplate> orderedArguments = emulator.arguments;
    std::stable_sort(orderedArguments.begin(), orderedArguments.end(), [](const auto& left, const auto& right) { return left.position < right.position; });
    QStringList argumentLines;
    for (const auto& arg : orderedArguments) argumentLines.append(arg.optional ? "?" + arg.templateString : arg.templateString);
    m_argumentsEdit->setPlainText(argumentLines.join('\n'));
    m_argumentsEdit->setFixedHeight(115);
    layout->addWidget(m_argumentsEdit);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto *saveBtn = new QPushButton("Save Profile", this);
    saveBtn->setStyleSheet("padding: 6px 14px; font-weight: bold; background-color: #2b78e4; color: white;");
    btnLayout->addWidget(saveBtn);

    auto *cancelBtn = new QPushButton("Cancel", this);
    btnLayout->addWidget(cancelBtn);

    layout->addLayout(btnLayout);

    connect(saveBtn, &QPushButton::clicked, this, &EditEmulatorDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void EditEmulatorDialog::accept() {
    if (m_programEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Executable program path cannot be empty.");
        return;
    }

    m_emulator.name = m_nameEdit->text().trimmed();
    m_emulator.launchType = static_cast<Domain::LaunchType>(m_launchTypeCombo->currentIndex());
    m_emulator.hidePolicy = static_cast<Domain::HidePolicy>(m_hidePolicyCombo->currentIndex());
    m_emulator.program = m_programEdit->text().trimmed();
    m_emulator.workingDirectory = m_workingDirEdit->text().trimmed();
    m_emulator.shellMode = m_shellModeCheck->isChecked();
    m_emulator.detach = m_detachCheck->isChecked();
    m_emulator.captureOutput = m_captureOutputCheck->isChecked();

    m_emulator.arguments.clear();
    const QStringList argumentLines = m_argumentsEdit->toPlainText().split('\n', Qt::SkipEmptyParts);
    for (const QString& line : argumentLines) {
        QString templateString = line.trimmed();
        if (templateString.isEmpty()) continue;
        Domain::ArgumentTemplate arg;
        arg.optional = templateString.startsWith('?');
        if (arg.optional) templateString.remove(0, 1);
        templateString = templateString.trimmed();
        if (templateString.isEmpty()) continue;
        arg.position = static_cast<int>(m_emulator.arguments.size());
        arg.templateString = templateString;
        m_emulator.arguments.append(arg);
    }

    QDialog::accept();
}

} // namespace LudoShelf::UI
