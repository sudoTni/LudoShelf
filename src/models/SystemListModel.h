#ifndef LUDOSHELF_MODELS_SYSTEMLISTMODEL_H
#define LUDOSHELF_MODELS_SYSTEMLISTMODEL_H

#include <QAbstractListModel>
#include <QList>
#include "../domain/System.h"

namespace LudoShelf::Models {

class SystemListModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum CustomRoles {
        SystemIdRole = Qt::UserRole + 1,
        GameCountRole,
        SystemObjectRole
    };

    explicit SystemListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSystems(const QList<Domain::System>& systems);
    Domain::System getSystem(int row) const;

private:
    QList<Domain::System> m_systems;
};

} // namespace LudoShelf::Models

#endif // LUDOSHELF_MODELS_SYSTEMLISTMODEL_H
