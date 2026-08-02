#ifndef LUDOSHELF_UI_EDITEMULATORDIALOG_H
#define LUDOSHELF_UI_EDITEMULATORDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QPushButton>

#include "../domain/EmulatorProfile.h"

namespace LudoShelf::UI {

class EditEmulatorDialog : public QDialog {
    Q_OBJECT
public:
    explicit EditEmulatorDialog(const Domain::EmulatorProfile& emulator, QWidget *parent = nullptr);

    Domain::EmulatorProfile updatedEmulator() const { return m_emulator; }

protected:
    void accept() override;

private:
    QLineEdit *m_nameEdit;
    QComboBox *m_launchTypeCombo;
    QComboBox *m_hidePolicyCombo;
    QLineEdit *m_programEdit;
    QLineEdit *m_workingDirEdit;
    QPlainTextEdit *m_argumentsEdit;
    QPlainTextEdit *m_environmentEdit;
    QCheckBox *m_shellModeCheck;
    QCheckBox *m_detachCheck;
    QCheckBox *m_captureOutputCheck;

    Domain::EmulatorProfile m_emulator;
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_EDITEMULATORDIALOG_H
