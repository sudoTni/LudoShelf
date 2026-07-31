#include "GamesGridView.h"
#include "../models/GameTableModel.h"

#include <QKeyEvent>

namespace LudoShelf::UI {

GamesGridView::GamesGridView(QWidget *parent)
    : QListView(parent)
    , m_delegate(new ArtworkDelegate(this))
{
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setResizeMode(QListView::Adjust);
    setSpacing(12);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setItemDelegate(m_delegate);

    connect(this, &QListView::doubleClicked, this, [this](const QModelIndex &index) {
        if (index.isValid() && m_proxyModel) {
            QUuid gameId = QUuid::fromString(m_proxyModel->data(index, Models::GameTableModel::GameIdRole).toString());
            emit gameActivated(gameId);
        }
    });

    connect(this, &QListView::clicked, this, [this](const QModelIndex &index) {
        if (index.isValid() && m_proxyModel) {
            QUuid gameId = QUuid::fromString(m_proxyModel->data(index, Models::GameTableModel::GameIdRole).toString());
            emit gameSelectionChanged(gameId);
        }
    });
}

void GamesGridView::setProxyModel(Models::GameFilterProxyModel *proxyModel) {
    m_proxyModel = proxyModel;
    setModel(proxyModel);

    connect(selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &current, const QModelIndex &previous) {
        Q_UNUSED(previous);
        if (current.isValid() && m_proxyModel) {
            QUuid gameId = QUuid::fromString(m_proxyModel->data(current, Models::GameTableModel::GameIdRole).toString());
            emit gameSelectionChanged(gameId);
        }
    });

    connect(selectionModel(), &QItemSelectionModel::selectionChanged,
            this, [this](const QItemSelection &selected, const QItemSelection &deselected) {
        Q_UNUSED(deselected);
        if (!selected.indexes().isEmpty() && m_proxyModel) {
            QModelIndex current = selected.indexes().first();
            QUuid gameId = QUuid::fromString(m_proxyModel->data(current, Models::GameTableModel::GameIdRole).toString());
            emit gameSelectionChanged(gameId);
        }
    });
}

void GamesGridView::setCoverAspect(CoverAspect aspect) {
    const auto delegateAspect = aspect == CoverAspect::Landscape
        ? ArtworkDelegate::CoverAspect::Landscape : ArtworkDelegate::CoverAspect::Portrait;
    if (m_delegate->coverAspect() == delegateAspect) return;
    m_delegate->setCoverAspect(delegateAspect);
    doItemsLayout();
    viewport()->update();
}

GamesGridView::CoverAspect GamesGridView::coverAspect() const {
    return m_delegate->coverAspect() == ArtworkDelegate::CoverAspect::Landscape
        ? CoverAspect::Landscape : CoverAspect::Portrait;
}

void GamesGridView::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QModelIndex current = currentIndex();
        if (current.isValid() && m_proxyModel) {
            QUuid gameId = QUuid::fromString(m_proxyModel->data(current, Models::GameTableModel::GameIdRole).toString());
            emit gameActivated(gameId);
            return;
        }
    }
    QListView::keyPressEvent(event);
}

} // namespace LudoShelf::UI
