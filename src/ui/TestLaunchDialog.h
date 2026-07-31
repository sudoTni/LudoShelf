#ifndef LUDOSHELF_UI_TESTLAUNCHDIALOG_H
#define LUDOSHELF_UI_TESTLAUNCHDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QLabel>
#include <QPushButton>

#include "../launch/LaunchService.h"

namespace LudoShelf::UI {

class TestLaunchDialog : public QDialog {
    Q_OBJECT
public:
    explicit TestLaunchDialog(const Launch::LaunchCommand& cmd, QWidget *parent = nullptr);

signals:
    void executeLaunchRequested();

private:
    QTextEdit *m_commandPreviewText;
    QLabel *m_programLabel;
    QLabel *m_workingDirLabel;
    QPushButton *m_copyBtn;
    QPushButton *m_launchBtn;
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_TESTLAUNCHDIALOG_H
