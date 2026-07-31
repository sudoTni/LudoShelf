#ifndef LUDOSHELF_UI_GAMESGRIDVIEW_H
#define LUDOSHELF_UI_GAMESGRIDVIEW_H

#include <QListView>
#include <QUuid>

#include "../models/GameFilterProxyModel.h"
#include "ArtworkDelegate.h"

namespace LudoShelf::UI {

class GamesGridView : public QListView {
    Q_OBJECT
public:
    enum class CoverAspect { Portrait, Landscape };

    explicit GamesGridView(QWidget *parent = nullptr);

    void setProxyModel(Models::GameFilterProxyModel *proxyModel);
    void setCoverAspect(CoverAspect aspect);
    CoverAspect coverAspect() const;

signals:
    void gameActivated(const QUuid& gameId);
    void gameSelectionChanged(const QUuid& gameId);

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    Models::GameFilterProxyModel *m_proxyModel{nullptr};
    ArtworkDelegate *m_delegate{nullptr};
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_GAMESGRIDVIEW_H
