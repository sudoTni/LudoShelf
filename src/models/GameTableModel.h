#ifndef LUDOSHELF_MODELS_GAMETABLEMODEL_H
#define LUDOSHELF_MODELS_GAMETABLEMODEL_H

#include <QAbstractTableModel>
#include <QHash>
#include <QList>
#include <QPixmap>
#include "../domain/Game.h"

namespace LudoShelf::Models {

class GameTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column {
        ColumnTitle = 0,
        ColumnRegion,
        ColumnReleaseDate,
        ColumnDeveloper,
        ColumnFavorite,
        ColumnStatus,
        ColumnLastPlayed,
        ColumnPlayCount,
        ColumnCount
    };

    enum CustomRoles {
        GameIdRole = Qt::UserRole + 1,
        SystemIdRole,
        GameObjectRole,
        CoverPixmapRole
    };

    explicit GameTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    void setGames(const QList<Domain::Game>& games);
    void updateGame(const Domain::Game& game);
    Domain::Game getGame(int row) const;

private:
    QList<Domain::Game> m_games;
    // Model roles can be requested repeatedly while a view is painting or
    // laying out.  Keep artwork resolution read-only and memoized so painting
    // never turns into a database-write or disk-I/O hot path.
    mutable QHash<QUuid, QPixmap> m_coverPixmapByGame;
    mutable QHash<QUuid, QString> m_systemNameById;
};

} // namespace LudoShelf::Models

#endif // LUDOSHELF_MODELS_GAMETABLEMODEL_H
