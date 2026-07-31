#ifndef LUDOSHELF_UI_COVERARTDIALOG_H
#define LUDOSHELF_UI_COVERARTDIALOG_H

#include <QDialog>
#include <QUuid>

class QTableWidget;
namespace LudoShelf::Covers { class CoverAcquisitionService; }

namespace LudoShelf::UI {

class CoverArtDialog : public QDialog {
    Q_OBJECT
public:
    explicit CoverArtDialog(const QUuid& gameId, QWidget *parent = nullptr);

private slots:
    void importUserCover();
    void useSelectedCover();
    void findLibretroCover();

private:
    void reloadAssets();

    QUuid m_gameId;
    QTableWidget *m_assetsTable{nullptr};
    Covers::CoverAcquisitionService *m_acquisition{nullptr};
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_COVERARTDIALOG_H
