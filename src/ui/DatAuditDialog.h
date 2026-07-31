#ifndef LUDOSHELF_UI_DATAUDITDIALOG_H
#define LUDOSHELF_UI_DATAUDITDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QTableWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QFutureWatcher>
#include <QUuid>

#include <atomic>
#include <memory>

#include "../dat/DatParser.h"
#include "../dat/HashService.h"

namespace LudoShelf::UI {

class DatAuditDialog : public QDialog {
    Q_OBJECT
public:
    explicit DatAuditDialog(const QUuid& systemId, QWidget *parent = nullptr);

private slots:
    void onBrowseDatClicked();
    void onRunAuditClicked();

private:
    struct AuditItem {
        QString title;
        Domain::GameFile file;
        Dat::FileHashes hashes;
        bool missing{false};
    };
    struct AuditRun {
        Dat::ParsedDatResult dat;
        QList<AuditItem> items;
        bool cancelled{false};
    };

    QUuid m_systemId;
    QLineEdit *m_datPathEdit;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;
    QTableWidget *m_resultsTable;
    QPushButton *m_auditBtn;
    QFutureWatcher<AuditRun> *m_auditWatcher{nullptr};
    std::shared_ptr<std::atomic_bool> m_cancelRequested;
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_DATAUDITDIALOG_H
