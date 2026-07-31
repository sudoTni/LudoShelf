#include "FilenameParser.h"

#include "../covers/CoverTitleNormalizer.h"

#include <QFileInfo>
#include <QRegularExpression>

namespace LudoShelf::Scanning {

ParsedFilenameInfo FilenameParser::parse(const QString& filename) {
    QFileInfo fi(filename);
    ParsedFilenameInfo info;
    info.rawFilename = fi.fileName();
    info.extension = fi.suffix().toLower();

    QString base = fi.completeBaseName();

    // Match region like (USA), (Europe), (Japan), (World)
    static QRegularExpression regionRegex(R"(\((USA|Europe|Japan|World|En,Fr,De|En|Fr|De|Es|It|Ja|Asia|Korea)\))", QRegularExpression::CaseInsensitiveOption);
    auto regionMatch = regionRegex.match(base);
    if (regionMatch.hasMatch()) {
        info.region = regionMatch.captured(1);
        base.remove(regionMatch.captured(0));
    }

    // Match revision like (Rev 1), (Rev A), (v1.0)
    static QRegularExpression revRegex(R"(\((Rev\s*\w+|v\d+\.\d+)\))", QRegularExpression::CaseInsensitiveOption);
    auto revMatch = revRegex.match(base);
    if (revMatch.hasMatch()) {
        info.revision = revMatch.captured(1);
        base.remove(revMatch.captured(0));
    }

    // Match dump status like [!], [b1], [o1]
    static QRegularExpression dumpRegex(R"(\[([^\]]+)\])", QRegularExpression::CaseInsensitiveOption);
    auto dumpMatches = dumpRegex.globalMatch(base);
    while (dumpMatches.hasNext()) {
        const QString tag = dumpMatches.next().captured(1).trimmed();
        if (tag == "!") info.dumpStatus = "Verified";
        else if (info.dumpStatus.isEmpty()) info.dumpStatus = tag;
    }
    base.remove(dumpRegex);

    const auto normalized = Covers::CoverTitleNormalizer::normalize(fi.completeBaseName());
    info.cleanTitle = normalized.canonicalTitle;
    if (info.region.isEmpty() && !normalized.extractedRegions.isEmpty()) info.region = normalized.extractedRegions.first();
    if (info.revision.isEmpty()) info.revision = normalized.revision;
    if (info.cleanTitle.isEmpty()) {
        info.cleanTitle = fi.completeBaseName();
    }


    return info;
}

} // namespace LudoShelf::Scanning
