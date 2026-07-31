#ifndef LUDOSHELF_DOMAIN_GAMEFILE_H
#define LUDOSHELF_DOMAIN_GAMEFILE_H

#include <QString>
#include <QUuid>
#include <QDateTime>

namespace LudoShelf::Domain {

enum class FileRole {
    Primary = 0,
    Disc,
    Track,
    Patch,
    SaveState
};

struct GameFile {
    QUuid id{QUuid::createUuid()};
    QUuid gameId;
    QString path;
    FileRole role{FileRole::Primary};
    int discNumber{0};
    qint64 fileSize{0};
    QDateTime modifiedTime;
    QString crc32;
    QString md5;
    QString sha1;
    QUuid datMatchId;
    bool available{true};
};

} // namespace LudoShelf::Domain

#endif // LUDOSHELF_DOMAIN_GAMEFILE_H
