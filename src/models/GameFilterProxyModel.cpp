#include "GameFilterProxyModel.h"
#include "GameTableModel.h"

namespace LudoShelf::Models {

GameFilterProxyModel::GameFilterProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent) {
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
    sort(GameTableModel::ColumnTitle, Qt::AscendingOrder);
}

void GameFilterProxyModel::setSystemFilter(const QUuid& systemId) {
    beginFilterChange();
    m_systemFilterId = systemId;
    endFilterChange();
}

void GameFilterProxyModel::setSearchText(const QString& text) {
    beginFilterChange();
    m_searchText = text.trimmed();
    endFilterChange();
}

void GameFilterProxyModel::setFavoritesOnly(bool favoritesOnly) {
    beginFilterChange();
    m_favoritesOnly = favoritesOnly;
    endFilterChange();
}

void GameFilterProxyModel::setStatusFilter(const QString& status) {
    beginFilterChange();
    m_statusFilter = status.trimmed();
    endFilterChange();
}

void GameFilterProxyModel::setGenreFilter(const QString& genre) {
    beginFilterChange();
    m_genreFilter = genre.trimmed();
    endFilterChange();
}

void GameFilterProxyModel::setRegionFilter(const QString& region) {
    beginFilterChange();
    m_regionFilter = region.trimmed();
    endFilterChange();
}

bool GameFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const {
    QModelIndex sysIdx = sourceModel()->index(sourceRow, 0, sourceParent);
    QUuid rowSystemId = QUuid::fromString(sourceModel()->data(sysIdx, GameTableModel::SystemIdRole).toString());

    if (!m_systemFilterId.isNull() && rowSystemId != m_systemFilterId) {
        return false;
    }

    if (m_favoritesOnly) {
        QModelIndex favIdx = sourceModel()->index(sourceRow, GameTableModel::ColumnFavorite, sourceParent);
        if (sourceModel()->data(favIdx).toString().isEmpty()) {
            return false;
        }
    }

    if (!m_statusFilter.isEmpty()) {
        QModelIndex statusIdx = sourceModel()->index(sourceRow, GameTableModel::ColumnStatus, sourceParent);
        if (sourceModel()->data(statusIdx).toString().compare(m_statusFilter, Qt::CaseInsensitive) != 0) {
            return false;
        }
    }

    if (!m_regionFilter.isEmpty()) {
        QModelIndex regionIdx = sourceModel()->index(sourceRow, GameTableModel::ColumnRegion, sourceParent);
        if (!sourceModel()->data(regionIdx).toString().contains(m_regionFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }

    if (!m_genreFilter.isEmpty()) {
        if (!sourceModel()->data(sysIdx, GameTableModel::GenreTextRole)
                 .toString().contains(m_genreFilter, Qt::CaseInsensitive)) {
            return false;
        }
    }

    if (!m_searchText.isEmpty()) {
        QModelIndex titleIdx = sourceModel()->index(sourceRow, GameTableModel::ColumnTitle, sourceParent);
        QModelIndex devIdx = sourceModel()->index(sourceRow, GameTableModel::ColumnDeveloper, sourceParent);

        QString title = sourceModel()->data(titleIdx).toString();
        QString dev = sourceModel()->data(devIdx).toString();

        if (!title.contains(m_searchText, Qt::CaseInsensitive) &&
            !dev.contains(m_searchText, Qt::CaseInsensitive)) {
            return false;
        }
    }

    return true;
}

bool GameFilterProxyModel::lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const {
    QVariant leftData = sourceModel()->data(sourceLeft);
    QVariant rightData = sourceModel()->data(sourceRight);

    if (leftData.userType() == QMetaType::Int) {
        return leftData.toInt() < rightData.toInt();
    }

    return QString::localeAwareCompare(leftData.toString().toCaseFolded(), rightData.toString().toCaseFolded()) < 0;
}

} // namespace LudoShelf::Models
