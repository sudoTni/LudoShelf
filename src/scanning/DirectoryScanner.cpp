#include "DirectoryScanner.h"
#include "FilenameParser.h"

#include <QDirIterator>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>

#include <algorithm>

namespace LudoShelf::Scanning {

DirectoryScanner::DirectoryScanner(QObject *parent)
    : QObject(parent) {}

QList<ScanCandidate> DirectoryScanner::scanDirectory(
    const QUuid& systemId,
    const QString& dirPath,
    const QStringList& allowedExtensions,
    bool recursive
) {
    ScanOptions options;
    options.allowedExtensions = allowedExtensions;
    options.recursive = recursive;
    return scanDirectory(systemId, dirPath, options);
}

QList<ScanCandidate> DirectoryScanner::scanDirectory(
    const QUuid& systemId,
    const QString& dirPath,
    const ScanOptions& options
) {
    QList<ScanCandidate> results;
    QDirIterator::IteratorFlags flags = QDirIterator::NoIteratorFlags;
    if (options.recursive) {
        flags |= QDirIterator::Subdirectories;
    }
    if (options.followSymlinks) flags |= QDirIterator::FollowSymlinks;

    const auto normalizedExtensions = [](const QStringList& values) {
        QSet<QString> result;
        for (QString value : values) {
            value = value.trimmed().toLower();
            while (value.startsWith('.')) value.remove(0, 1);
            if (!value.isEmpty()) result.insert(value);
        }
        return result;
    };
    const QSet<QString> allowed = normalizedExtensions(options.allowedExtensions);
    const QSet<QString> excluded = normalizedExtensions(options.excludedExtensions);
    QList<QRegularExpression> excludedPatterns;
    for (const QString& pattern : options.excludedPatterns) {
        const QString trimmed = pattern.trimmed();
        if (!trimmed.isEmpty()) excludedPatterns.append(QRegularExpression(
            QRegularExpression::wildcardToRegularExpression(trimmed),
            QRegularExpression::CaseInsensitiveOption));
    }

    QDirIterator it(dirPath, QDir::Files, flags);
    while (it.hasNext()) {
        it.next();
        QFileInfo fi = it.fileInfo();
        const QString ext = fi.suffix().toLower();

        if ((!allowed.isEmpty() && !allowed.contains(ext)) || excluded.contains(ext)) {
            continue;
        }
        const QString relativePath = QDir(dirPath).relativeFilePath(fi.absoluteFilePath());
        const bool patternExcluded = std::any_of(excludedPatterns.cbegin(), excludedPatterns.cend(),
            [&fi, &relativePath](const QRegularExpression& pattern) {
                return pattern.match(fi.fileName()).hasMatch() || pattern.match(relativePath).hasMatch();
            });
        if (patternExcluded) continue;

        ParsedFilenameInfo parsed = FilenameParser::parse(fi.fileName());

        ScanCandidate candidate;
        candidate.game.systemId = systemId;
        candidate.game.title = parsed.cleanTitle;
        candidate.game.region = parsed.region;
        candidate.game.dateAdded = QDateTime::currentDateTimeUtc();

        candidate.file.path = fi.absoluteFilePath();
        candidate.file.fileSize = fi.size();
        candidate.file.modifiedTime = fi.lastModified();
        candidate.file.available = true;

        results.append(candidate);
    }

    return results;
}

} // namespace LudoShelf::Scanning
