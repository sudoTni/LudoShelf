#ifndef LUDOSHELF_DOMAIN_PLAYSESSION_H
#define LUDOSHELF_DOMAIN_PLAYSESSION_H

#include <QString>
#include <QUuid>
#include <QDateTime>

namespace LudoShelf::Domain {

struct PlaySession {
    QUuid id{QUuid::createUuid()};
    QUuid gameId;
    QUuid emulatorId;
    QDateTime startedAt{QDateTime::currentDateTimeUtc()};
    QDateTime endedAt;
    int durationSeconds{0};
    int exitCode{0};
    int exitStatus{0};
    QString launchError;
};

} // namespace LudoShelf::Domain

#endif // LUDOSHELF_DOMAIN_PLAYSESSION_H
