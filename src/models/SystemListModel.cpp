#include "SystemListModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>

namespace LudoShelf::Models {

SystemListModel::SystemListModel(QObject *parent)
    : QAbstractListModel(parent) {}

int SystemListModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_systems.size());
}

QVariant SystemListModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_systems.size()) {
        return {};
    }

    const auto& sys = m_systems[index.row()];

    switch (role) {
    case Qt::DisplayRole:
        return QString("%1 (%2)").arg(sys.name).arg(sys.gameCount);
    case Qt::DecorationRole: {
        if (sys.iconPath.trimmed().isEmpty()) return {};
        const QFileInfo stored(sys.iconPath);
        const QString path = stored.isRelative()
            ? QDir(QCoreApplication::applicationDirPath()).filePath(sys.iconPath)
            : stored.absoluteFilePath();
        const QIcon icon(path);
        return icon.isNull() ? QVariant() : QVariant::fromValue(icon);
    }
    case SystemIdRole:
        return sys.id.toString(QUuid::WithBraces);
    case GameCountRole:
        return sys.gameCount;
    default:
        return {};
    }
}

QHash<int, QByteArray> SystemListModel::roleNames() const {
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[SystemIdRole] = "systemId";
    roles[GameCountRole] = "gameCount";
    return roles;
}

void SystemListModel::setSystems(const QList<Domain::System>& systems) {
    beginResetModel();
    m_systems = systems;
    endResetModel();
}

Domain::System SystemListModel::getSystem(int row) const {
    if (row >= 0 && row < m_systems.size()) {
        return m_systems[row];
    }
    return {};
}

} // namespace LudoShelf::Models
