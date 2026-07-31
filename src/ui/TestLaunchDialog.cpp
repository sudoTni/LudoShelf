#include "TestLaunchDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGuiApplication>
#include <QClipboard>

namespace LudoShelf::UI {

TestLaunchDialog::TestLaunchDialog(const Launch::LaunchCommand& cmd, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("Test Launch Command");
    resize(600, 450);

    auto *layout = new QVBoxLayout(this);

    auto *form = new QFormLayout();

    m_programLabel = new QLabel(cmd.program, this);
    m_programLabel->setStyleSheet("font-weight: bold; font-family: monospace;");
    form->addRow("Executable:", m_programLabel);

    m_workingDirLabel = new QLabel(cmd.workingDirectory.isEmpty() ? "(Default)" : cmd.workingDirectory, this);
    form->addRow("Working Directory:", m_workingDirLabel);

    layout->addLayout(form);

    layout->addWidget(new QLabel("Resolved Arguments (line-by-line):", this));

    m_commandPreviewText = new QTextEdit(this);
    m_commandPreviewText->setReadOnly(true);
    m_commandPreviewText->setStyleSheet("font-family: monospace; background-color: #1e1e1e; color: #d4d4d4;");

    QString fullPreview;
    fullPreview += "Program:\n  " + cmd.program + "\n\nArguments:\n";
    for (int i = 0; i < cmd.arguments.size(); ++i) {
        fullPreview += QString("[%1] %2\n").arg(i).arg(cmd.arguments[i]);
    }
    m_commandPreviewText->setText(fullPreview);
    layout->addWidget(m_commandPreviewText);

    auto *btnLayout = new QHBoxLayout();
    m_copyBtn = new QPushButton("Copy Diagnostic Command", this);
    btnLayout->addWidget(m_copyBtn);

    btnLayout->addStretch();

    m_launchBtn = new QPushButton("▶ Launch Game", this);
    m_launchBtn->setStyleSheet("padding: 6px 16px; font-weight: bold; background-color: #2b78e4; color: white;");
    btnLayout->addWidget(m_launchBtn);

    auto *closeBtn = new QPushButton("Close", this);
    btnLayout->addWidget(closeBtn);

    layout->addLayout(btnLayout);

    connect(m_copyBtn, &QPushButton::clicked, this, [cmd]() {
        QString singleLine = cmd.program;
        for (const auto& arg : cmd.arguments) {
            if (arg.contains(' ') || arg.contains('"')) {
                singleLine += " \"" + QString(arg).replace('"', "\\\"") + "\"";
            } else {
                singleLine += " " + arg;
            }
        }
        QGuiApplication::clipboard()->setText(singleLine);
    });

    connect(m_launchBtn, &QPushButton::clicked, this, [this]() {
        emit executeLaunchRequested();
        accept();
    });

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::reject);
}

} // namespace LudoShelf::UI
