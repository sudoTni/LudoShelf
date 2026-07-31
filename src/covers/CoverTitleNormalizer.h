#ifndef LUDOSHELF_COVERS_COVERTITLENORMALIZER_H
#define LUDOSHELF_COVERS_COVERTITLENORMALIZER_H

#include <QString>
#include <QStringList>

namespace LudoShelf::Covers {

struct NormalizedCoverLookup {
    QString original;
    QString unicodeNormalized;
    QString caseFolded;
    QString punctuationNormalized;
    QString canonicalTitle;
    QString shortTitle;
    QString libretroFilename;
    QStringList extractedRegions;
    QString edition;
    QString revision;
    int discNumber{0};
};

class CoverTitleNormalizer {
public:
    static NormalizedCoverLookup normalize(const QString& title);
    static QStringList libretroTitleCandidates(const QStringList& titles, const QString& fallbackRegion = {});
    static QString libretroSanitize(QString title);
};

} // namespace LudoShelf::Covers

#endif // LUDOSHELF_COVERS_COVERTITLENORMALIZER_H
