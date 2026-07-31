#ifndef LUDOSHELF_DAT_DATPARSER_H
#define LUDOSHELF_DAT_DATPARSER_H

#include <QString>
#include <QList>

#include "../database/DatabaseManager.h"

namespace LudoShelf::Dat {

struct ParsedDatHeader {
    QString name;
    QString version;
    QString author;
    QString category;
};

struct ParsedDatResult {
    ParsedDatHeader header;
    QList<Database::DatEntry> entries;
    bool success{false};
    QString errorMessage;
};

class DatParser {
public:
    static ParsedDatResult parseDatFile(const QString& filePath);

private:
    static ParsedDatResult parseLogiqxXml(const QString& filePath);
    static ParsedDatResult parseClrMamePro(const QString& filePath);
};

} // namespace LudoShelf::Dat

#endif // LUDOSHELF_DAT_DATPARSER_H
