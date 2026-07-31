#include "GamesTableView.h"
#include "../models/GameTableModel.h"
#include "../app/AppPaths.h"

#include <QHeaderView>
#include <QKeyEvent>
#include <QSettings>
#include <QMenu>

namespace LudoShelf::UI {

GamesTableView::GamesTableView(QWidget *parent)
    : QTableView(parent)
{
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::SingleSelection);
    // ROM lists have one stable order in both table and grid views.
    setSortingEnabled(false);
    setAlternatingRowColors(true);
    horizontalHeader()->setStretchLastSection(true);
    horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    verticalHeader()->hide();

    connect(this, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        if (index.isValid() && m_proxyModel) {
            QUuid gameId = QUuid::fromString(m_proxyModel->data(index, Models::GameTableModel::GameIdRole).toString());
            emit gameActivated(gameId);
        }
    });

    connect(this, &QTableView::clicked, this, [this](const QModelIndex &index) {
        if (index.isValid() && m_proxyModel) {
            QUuid gameId = QUuid::fromString(m_proxyModel->data(index, Models::GameTableModel::GameIdRole).toString());
            emit gameSelectionChanged(gameId);
        }
    });

    connect(horizontalHeader(), &QHeaderView::sectionResized, this, &GamesTableView::saveHeaderState);
    connect(horizontalHeader(), &QHeaderView::customContextMenuRequested, this, [this](const QPoint& pos) {
        QMenu menu(this);
        menu.addAction("Reset Column Widths to Default", this, &GamesTableView::resetColumnWidths);
        menu.exec(horizontalHeader()->mapToGlobal(pos));
    });
}

void GamesTableView::setProxyModel(Models::GameFilterProxyModel *proxyModel) {
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

    loadHeaderState();
}

void GamesTableView::saveHeaderState() {
    QSettings settings(App::AppPaths::settingsPath(), QSettings::IniFormat);
    settings.setValue("GamesTableView/HeaderState", horizontalHeader()->saveState());
}

void GamesTableView::loadHeaderState() {
    QSettings settings(App::AppPaths::settingsPath(), QSettings::IniFormat);
    QByteArray headerState = settings.value("GamesTableView/HeaderState").toByteArray();
    if (!headerState.isEmpty()) {
        horizontalHeader()->restoreState(headerState);
    } else {
        resetColumnWidths();
    }
}

void GamesTableView::resetColumnWidths() {
    QSettings settings(App::AppPaths::settingsPath(), QSettings::IniFormat);
    settings.remove("GamesTableView/HeaderState");

    setColumnWidth(Models::GameTableModel::ColumnTitle, 240);
    setColumnWidth(Models::GameTableModel::ColumnRegion, 80);
    setColumnWidth(Models::GameTableModel::ColumnReleaseDate, 100);
    setColumnWidth(Models::GameTableModel::ColumnDeveloper, 140);
    setColumnWidth(Models::GameTableModel::ColumnFavorite, 45);
    setColumnWidth(Models::GameTableModel::ColumnStatus, 90);
    setColumnWidth(Models::GameTableModel::ColumnLastPlayed, 140);
    setColumnWidth(Models::GameTableModel::ColumnPlayCount, 80);
}

void GamesTableView::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        QModelIndex current = currentIndex();
        if (current.isValid() && m_proxyModel) {
            QUuid gameId = QUuid::fromString(m_proxyModel->data(current, Models::GameTableModel::GameIdRole).toString());
            emit gameActivated(gameId);
            return;
        }
    }
    QTableView::keyPressEvent(event);
}

} // namespace LudoShelf::UI
