#ifndef LUDOSHELF_UI_EDITSYSTEMDIALOG_H
#define LUDOSHELF_UI_EDITSYSTEMDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QCheckBox>
#include <QListWidget>
#include <QComboBox>

#include "../domain/System.h"
#include "../database/DatabaseManager.h"

namespace LudoShelf::UI {

class EditSystemDialog : public QDialog {
    Q_OBJECT
public:
    explicit EditSystemDialog(const Domain::System& system, QWidget *parent = nullptr);

    Domain::System updatedSystem() const { return m_system; }
    Database::ScanRoot updatedScanRoot() const { return m_scanRoot; }
    bool hasConfiguredScanRoot() const { return !m_scanRoot.path.isEmpty(); }
    bool hadScanRoot() const { return m_hadScanRoot; }

protected:
    void accept() override;

private:
    QLineEdit *m_nameEdit;
    QComboBox *m_presetCombo;
    QLineEdit *m_shortNameEdit;
    QLineEdit *m_manufacturerEdit;
    QListWidget *m_iconList;
    QSpinBox *m_releaseYearSpin;
    QLineEdit *m_romDirectoryEdit;
    QLineEdit *m_extensionsEdit;
    QLineEdit *m_excludedExtensionsEdit;
    QLineEdit *m_excludedPatternsEdit;
    QCheckBox *m_recursiveCheck;
    QCheckBox *m_followSymlinksCheck;
    QCheckBox *m_watchChangesCheck;
    QTextEdit *m_notesText;

    Domain::System m_system;
    Database::ScanRoot m_scanRoot;
    bool m_hadScanRoot{false};
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_EDITSYSTEMDIALOG_H
