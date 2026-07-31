#ifndef LUDOSHELF_DOMAIN_SYSTEM_H
#define LUDOSHELF_DOMAIN_SYSTEM_H

#include <QString>
#include <QUuid>
#include <QDateTime>

namespace LudoShelf::Domain {

struct System {
    QUuid id{QUuid::createUuid()};
    QString name;
    QString sortName;
    QString shortName;
    QString manufacturer;
    int releaseYear{0};
    QString iconPath;
    QString notes;
    QDateTime createdAt{QDateTime::currentDateTimeUtc()};
    QDateTime updatedAt{QDateTime::currentDateTimeUtc()};
    int sortOrder{0};
    bool enabled{true};
    int gameCount{0};
};

} // namespace LudoShelf::Domain

#endif // LUDOSHELF_DOMAIN_SYSTEM_H
