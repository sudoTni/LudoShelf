#ifndef LUDOSHELF_UI_GAMESTABLEVIEW_H
#define LUDOSHELF_UI_GAMESTABLEVIEW_H

#include <QTableView>
#include <QUuid>
#include "../models/GameFilterProxyModel.h"

namespace LudoShelf::UI {

class GamesTableView : public QTableView {
    Q_OBJECT
public:
    explicit GamesTableView(QWidget *parent = nullptr);

    void setProxyModel(Models::GameFilterProxyModel *proxyModel);
    void resetColumnWidths();

signals:
    void gameActivated(const QUuid& gameId);
    void gameSelectionChanged(const QUuid& gameId);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void saveHeaderState();

private:
    void loadHeaderState();

    Models::GameFilterProxyModel *m_proxyModel{nullptr};
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_GAMESTABLEVIEW_H
