#ifndef LUDOSHELF_DOMAIN_GAME_H
#define LUDOSHELF_DOMAIN_GAME_H

#include <QString>
#include <QUuid>
#include <QDateTime>
#include <QDate>
#include <QStringList>

namespace LudoShelf::Domain {

struct Game {
    QUuid id{QUuid::createUuid()};
    QUuid systemId;
    QString title;
    QString sortTitle;
    QString description;
    QDate releaseDate;
    QString developer;
    QString publisher;
    QString region;
    QStringList languages;
    QStringList genres;
    QString series;
    int playersMin{1};
    int playersMax{1};
    bool favorite{false};
    double userRating{0.0};
    QString status{"Unplayed"};
    QString notes;
    QDateTime dateAdded{QDateTime::currentDateTimeUtc()};
    QDateTime lastPlayed;
    int playCount{0};
    int totalPlaySeconds{0};
    QUuid emulatorOverrideId;
    bool missing{false};
};

} // namespace LudoShelf::Domain

#endif // LUDOSHELF_DOMAIN_GAME_H
