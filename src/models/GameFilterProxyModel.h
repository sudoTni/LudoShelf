#ifndef LUDOSHELF_MODELS_GAMEFILTERPROXYMODEL_H
#define LUDOSHELF_MODELS_GAMEFILTERPROXYMODEL_H

#include <QSortFilterProxyModel>
#include <QUuid>

namespace LudoShelf::Models {

class GameFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit GameFilterProxyModel(QObject *parent = nullptr);

    void setSystemFilter(const QUuid& systemId);
    void setSearchText(const QString& text);
    void setFavoritesOnly(bool favoritesOnly);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    bool lessThan(const QModelIndex &sourceLeft, const QModelIndex &sourceRight) const override;

private:
    QUuid m_systemFilterId;
    QString m_searchText;
    bool m_favoritesOnly{false};
};

} // namespace LudoShelf::Models

#endif // LUDOSHELF_MODELS_GAMEFILTERPROXYMODEL_H
