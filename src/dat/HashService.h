#ifndef LUDOSHELF_DAT_HASHSERVICE_H
#define LUDOSHELF_DAT_HASHSERVICE_H

#include <QString>

namespace LudoShelf::Dat {

struct FileHashes {
    QString crc32;
    QString md5;
    QString sha1;
    bool success{false};
};

class HashService {
public:
    static FileHashes calculateHashes(const QString& filePath);
};

} // namespace LudoShelf::Dat

#endif // LUDOSHELF_DAT_HASHSERVICE_H
