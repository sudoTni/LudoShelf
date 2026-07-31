#include "GameTableModel.h"
#include "../database/DatabaseManager.h"
#include "../media/MediaStorageManager.h"
#include "../media/PlaceholderGenerator.h"

#include <QFileInfo>
#include <QTimeZone>

#include <algorithm>

namespace LudoShelf::Models {

GameTableModel::GameTableModel(QObject *parent)
    : QAbstractTableModel(parent) {}

int GameTableModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_games.size());
}

int GameTableModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return ColumnCount;
}

QVariant GameTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_games.size()) {
        return {};
    }

    const auto& g = m_games[index.row()];

    if (role == GameIdRole) return g.id.toString(QUuid::WithBraces);
    if (role == SystemIdRole) return g.systemId.toString(QUuid::WithBraces);

    if (role == CoverPixmapRole) {
        const auto cached = m_coverPixmapByGame.constFind(g.id);
        if (cached != m_coverPixmapByGame.cend()) return cached.value();

        const auto preferredAsset = Database::DatabaseManager::instance().getPreferredCoverAsset(g.id);
        QString sha256 = preferredAsset.mediaObjectSha256;

        if (sha256.isEmpty()) {
            auto media = Database::DatabaseManager::instance().getMediaForGame(g.id);
            for (const auto& item : media) {
                if (!item.path.isEmpty()) {
                    sha256 = QFileInfo(item.path).completeBaseName();
                    if (item.preferred) break;
                }
            }
        }

        QPixmap pixmap;
        if (!sha256.isEmpty()) {
            const QImage img = Media::MediaStorageManager::instance().loadThumbnail(sha256, 140, 210);
            if (!img.isNull()) {
                pixmap = QPixmap::fromImage(img);
            }
        }

        if (pixmap.isNull()) {
            QString systemName = m_systemNameById.value(g.systemId);
            if (systemName.isEmpty()) {
                systemName = Database::DatabaseManager::instance().getSystem(g.systemId).name;
                m_systemNameById.insert(g.systemId, systemName);
            }
            // Placeholder artwork is a display fallback, not user data.  Do
            // not write it to the database from a paint role; actual cover
            // acquisition still persists its deliberate placeholders.
            pixmap = QPixmap::fromImage(Media::PlaceholderGenerator::generatePlaceholderImage(
                g.title, systemName, 140, 210));
        }
        m_coverPixmapByGame.insert(g.id, pixmap);
        return pixmap;
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case ColumnTitle: return g.title;
        case ColumnRegion: return g.region.isEmpty() ? "-" : g.region;
        case ColumnReleaseDate:
            if (!g.releaseDate.isValid()) return "-";
            // Libretro supplies a release year for most titles.  We retain it
            // as Jan 1 internally, but avoid presenting that as a precise date.
            return g.releaseDate.month() == 1 && g.releaseDate.day() == 1
                ? g.releaseDate.toString("yyyy") : g.releaseDate.toString("yyyy-MM-dd");
        case ColumnDeveloper: return g.developer.isEmpty() ? "-" : g.developer;
        case ColumnFavorite: return g.favorite ? "★" : "";
        case ColumnStatus: return g.status;
        case ColumnLastPlayed: {
            if (!g.lastPlayed.isValid()) return "Never";
            static const QTimeZone easternTime("America/New_York");
            const QDateTime eastern = easternTime.isValid() ? g.lastPlayed.toTimeZone(easternTime) : g.lastPlayed.toLocalTime();
            return eastern.toString("yyyy-MM-dd HH:mm t");
        }
        case ColumnPlayCount: return g.playCount;
        default: break;
        }
    }

    return {};
}

QVariant GameTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        switch (section) {
        case ColumnTitle: return "Title";
        case ColumnRegion: return "Region";
        case ColumnReleaseDate: return "Release Date";
        case ColumnDeveloper: return "Developer";
        case ColumnFavorite: return "Fav";
        case ColumnStatus: return "Status";
        case ColumnLastPlayed: return "Last Played";
        case ColumnPlayCount: return "Play Count";
        default: break;
        }
    }
    return {};
}

void GameTableModel::setGames(const QList<Domain::Game>& games) {
    beginResetModel();
    m_games = games;
    m_coverPixmapByGame.clear();
    m_systemNameById.clear();
    std::stable_sort(m_games.begin(), m_games.end(), [](const Domain::Game& left, const Domain::Game& right) {
        const int comparison = QString::localeAwareCompare(left.title.toCaseFolded(), right.title.toCaseFolded());
        if (comparison != 0) return comparison < 0;
        return left.id.toString(QUuid::WithoutBraces) < right.id.toString(QUuid::WithoutBraces);
    });
    endResetModel();
}

void GameTableModel::updateGame(const Domain::Game& game) {
    const auto it = std::find_if(m_games.begin(), m_games.end(), [&game](const Domain::Game& existing) {
        return existing.id == game.id;
    });
    if (it == m_games.end()) return;
    const int row = static_cast<int>(std::distance(m_games.begin(), it));
    *it = game;
    m_coverPixmapByGame.remove(game.id);
    emit dataChanged(index(row, ColumnRegion), index(row, ColumnDeveloper), {Qt::DisplayRole});
}

Domain::Game GameTableModel::getGame(int row) const {
    if (row >= 0 && row < m_games.size()) {
        return m_games[row];
    }
    return {};
}

} // namespace LudoShelf::Models
