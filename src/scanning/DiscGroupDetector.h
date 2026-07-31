#ifndef LUDOSHELF_SCANNING_DISCGROUPDETECTOR_H
#define LUDOSHELF_SCANNING_DISCGROUPDETECTOR_H

#include <QString>
#include <QStringList>
#include <QSet>

namespace LudoShelf::Scanning {

struct DiscGroupInfo {
    bool isDescriptor{false};
    QString descriptorPath;
    QStringList referencedFiles;
    int discNumber{1};
};

class DiscGroupDetector {
public:
    static DiscGroupInfo analyzeFile(const QString& filePath);
    static QStringList parseM3u(const QString& m3uPath);
    static QStringList parseCue(const QString& cuePath);
    static QStringList parseGdi(const QString& gdiPath);
};

} // namespace LudoShelf::Scanning

#endif // LUDOSHELF_SCANNING_DISCGROUPDETECTOR_H
