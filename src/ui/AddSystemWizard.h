#ifndef LUDOSHELF_UI_ADDSYSTEMWIZARD_H
#define LUDOSHELF_UI_ADDSYSTEMWIZARD_H

#include <QWizard>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QCheckBox>
#include <QTableWidget>
#include <QLabel>
#include <QListWidget>
#include <QComboBox>

#include "../domain/System.h"
#include "../domain/EmulatorProfile.h"
#include "../scanning/DirectoryScanner.h"

namespace LudoShelf::UI {

class AddSystemWizard : public QWizard {
    Q_OBJECT
public:
    explicit AddSystemWizard(QWidget *parent = nullptr);

    Domain::System createdSystem() const { return m_system; }
    QList<QUuid> importedGameIds() const { return m_importedGameIds; }

protected:
    void accept() override;

private:
    QWizardPage* createIdentityPage();
    QWizardPage* createLocationPage();
    QWizardPage* createEmulatorPage();
    QWizardPage* createPreviewPage();

    // Page 1 fields
    QComboBox *m_presetCombo;
    QLineEdit *m_nameEdit;
    QLineEdit *m_shortNameEdit;
    QLineEdit *m_manufacturerEdit;
    QListWidget *m_iconList;

    // Page 2 fields
    QLineEdit *m_romDirEdit;
    QLineEdit *m_extensionsEdit;
    QCheckBox *m_recursiveCheck;

    // Page 3 fields
    QLineEdit *m_emulatorNameEdit;
    QLineEdit *m_emulatorPathEdit;
    QPlainTextEdit *m_argumentsEdit;

    // Page 4 fields
    QTableWidget *m_previewTable;
    QLabel *m_previewCountLabel;

    Domain::System m_system;
    QList<Scanning::ScanCandidate> m_discoveredCandidates;
    QList<QUuid> m_importedGameIds;
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_ADDSYSTEMWIZARD_H
