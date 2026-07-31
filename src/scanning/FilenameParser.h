#ifndef LUDOSHELF_SCANNING_FILENAMEPARSER_H
#define LUDOSHELF_SCANNING_FILENAMEPARSER_H

#include <QString>

namespace LudoShelf::Scanning {

struct ParsedFilenameInfo {
    QString rawFilename;
    QString cleanTitle;
    QString region;
    QString revision;
    QString dumpStatus;
    QString extension;
};

class FilenameParser {
public:
    static ParsedFilenameInfo parse(const QString& filename);
};

} // namespace LudoShelf::Scanning

#endif // LUDOSHELF_SCANNING_FILENAMEPARSER_H
