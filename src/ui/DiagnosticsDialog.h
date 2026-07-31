#ifndef LUDOSHELF_UI_DIAGNOSTICSDIALOG_H
#define LUDOSHELF_UI_DIAGNOSTICSDIALOG_H

#include <QDialog>
#include <QTextEdit>
#include <QPushButton>

namespace LudoShelf::UI {

class DiagnosticsDialog : public QDialog {
    Q_OBJECT
public:
    explicit DiagnosticsDialog(QWidget *parent = nullptr);

private:
    void populateDiagnostics();

    QTextEdit *m_outputText;
    QPushButton *m_copyBtn;
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_DIAGNOSTICSDIALOG_H
