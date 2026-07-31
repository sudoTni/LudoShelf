#include "CoverTitleNormalizer.h"

#include <QRegularExpression>

namespace LudoShelf::Covers {

NormalizedCoverLookup CoverTitleNormalizer::normalize(const QString& title) {
    NormalizedCoverLookup result;
    result.original = title.trimmed();
    result.unicodeNormalized = result.original.normalized(QString::NormalizationForm_C);
    result.caseFolded = result.unicodeNormalized.toCaseFolded();

    QString canonical = result.unicodeNormalized;
    static const QRegularExpression bracketTag(R"(\s*\[[^\]]+\])");
    canonical.remove(bracketTag);
    static const QRegularExpression parenthesized(R"(\s*\(([^)]*)\))");
    QRegularExpressionMatchIterator matches = parenthesized.globalMatch(canonical);
    QList<QPair<int, int>> removals;
    const auto addRegion = [&result](const QString& value) {
        if (!value.isEmpty() && !result.extractedRegions.contains(value, Qt::CaseInsensitive)) result.extractedRegions.append(value);
    };
    for (; matches.hasNext();) {
        const auto match = matches.next();
        const QString token = match.captured(1).trimmed();
        const QString upper = token.toUpper();
        bool remove = false;
        if (upper == "USA" || upper == "US" || upper == "U") { addRegion("USA"); remove = true; }
        else if (upper == "EUROPE" || upper == "EUR" || upper == "E") { addRegion("Europe"); remove = true; }
        else if (upper == "JAPAN" || upper == "JP" || upper == "J") { addRegion("Japan"); remove = true; }
        else if (upper == "WORLD" || upper == "W") { addRegion("World"); remove = true; }
        else if (upper == "ASIA" || upper == "A") { addRegion("Asia"); remove = true; }
        else if (upper == "KOREA" || upper == "K") { addRegion("Korea"); remove = true; }
        else if (QRegularExpression(QStringLiteral("^[UEJAK]{2,5}$")).match(upper).hasMatch()) {
            for (const QChar code : upper) {
                if (code == 'U') addRegion("USA"); else if (code == 'E') addRegion("Europe");
                else if (code == 'J') addRegion("Japan"); else if (code == 'A') addRegion("Asia"); else if (code == 'K') addRegion("Korea");
            }
            remove = true;
        } else if (QRegularExpression(QStringLiteral("^REV(?:ISION)?\\s*[A-Z0-9.]+$"), QRegularExpression::CaseInsensitiveOption).match(token).hasMatch() ||
                   QRegularExpression(QStringLiteral("^V\\d+(?:\\.\\d+)*$"), QRegularExpression::CaseInsensitiveOption).match(token).hasMatch()) {
            result.revision = token; remove = true;
        } else if (QRegularExpression(QStringLiteral("^(?:DISC|DISK)\\s*\\d+$"), QRegularExpression::CaseInsensitiveOption).match(token).hasMatch()) {
            result.discNumber = token.section(QRegularExpression("\\s+"), -1).toInt(); remove = true;
        } else if (QRegularExpression(QStringLiteral("^[A-Z]{2}(?:,[A-Z]{2})+$"), QRegularExpression::CaseInsensitiveOption).match(token).hasMatch()) {
            remove = true;
        } else if (QRegularExpression(QStringLiteral("^\\d$")) .match(token).hasMatch()) {
            // GoodGen's terminal single-number flag is not part of the title.
            remove = true;
        }
        if (remove) removals.prepend({match.capturedStart(0), match.capturedLength(0)});
    }
    for (const auto& removal : removals) canonical.remove(removal.first, removal.second);
    canonical = canonical.simplified();
    result.canonicalTitle = canonical;
    result.shortTitle = canonical.section('(', 0, 0).trimmed();

    result.punctuationNormalized = canonical.toCaseFolded();
    result.punctuationNormalized.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(" "));
    result.punctuationNormalized = result.punctuationNormalized.simplified();
    result.libretroFilename = libretroSanitize(canonical);
    return result;
}

QStringList CoverTitleNormalizer::libretroTitleCandidates(const QStringList& titles, const QString& fallbackRegion) {
    QStringList result;
    const auto append = [&result](const QString& value) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty() && !result.contains(trimmed, Qt::CaseInsensitive)) result.append(trimmed);
    };
    for (const QString& title : titles) {
        append(title);
        const auto normalized = normalize(title);
        append(normalized.canonicalTitle);
        QStringList regions = normalized.extractedRegions;
        if (regions.isEmpty() && !fallbackRegion.trimmed().isEmpty()) regions.append(fallbackRegion.trimmed());
        for (const QString& region : regions) append(normalized.canonicalTitle + QStringLiteral(" (%1)").arg(region));
        append(normalized.shortTitle);
    }
    return result;
}

QString CoverTitleNormalizer::libretroSanitize(QString title) {
    static const QRegularExpression invalid(QStringLiteral(R"([&*/:`<>?\\|"])"));
    title.replace(invalid, QStringLiteral("_"));
    return title;
}

} // namespace LudoShelf::Covers
