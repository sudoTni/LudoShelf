#include "DiscGroupDetector.h"

#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

namespace LudoShelf::Scanning {

DiscGroupInfo DiscGroupDetector::analyzeFile(const QString& filePath) {
    DiscGroupInfo info;
    QFileInfo fi(filePath);
    QString ext = fi.suffix().toLower();

    static QRegularExpression discNumRegex(R"raw((Disc|Cd|Disk)\s*(\d+))raw", QRegularExpression::CaseInsensitiveOption);
    auto match = discNumRegex.match(fi.completeBaseName());
    if (match.hasMatch()) {
        info.discNumber = match.captured(2).toInt();
    }

    if (ext == "m3u") {
        info.isDescriptor = true;
        info.descriptorPath = filePath;
        info.referencedFiles = parseM3u(filePath);
    } else if (ext == "cue") {
        info.isDescriptor = true;
        info.descriptorPath = filePath;
        info.referencedFiles = parseCue(filePath);
    } else if (ext == "gdi") {
        info.isDescriptor = true;
        info.descriptorPath = filePath;
        info.referencedFiles = parseGdi(filePath);
    }

    return info;
}

QStringList DiscGroupDetector::parseM3u(const QString& m3uPath) {
    QStringList files;
    QFile f(m3uPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        QFileInfo m3uFi(m3uPath);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (!line.isEmpty() && !line.startsWith("#")) {
                QFileInfo refFi(m3uFi.dir(), line);
                files.append(refFi.absoluteFilePath());
            }
        }
    }
    return files;
}

QStringList DiscGroupDetector::parseCue(const QString& cuePath) {
    QStringList files;
    QFile f(cuePath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        QFileInfo cueFi(cuePath);
        static QRegularExpression fileRegex(R"raw(FILE\s+"([^"]+)")raw", QRegularExpression::CaseInsensitiveOption);
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            auto match = fileRegex.match(line);
            if (match.hasMatch()) {
                QFileInfo refFi(cueFi.dir(), match.captured(1));
                files.append(refFi.absoluteFilePath());
            }
        }
    }
    return files;
}

QStringList DiscGroupDetector::parseGdi(const QString& gdiPath) {
    QStringList files;
    QFile f(gdiPath);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QTextStream in(&f);
        QFileInfo gdiFi(gdiPath);
        static QRegularExpression trackRegex(R"raw(\d+\s+\d+\s+\d+\s+\d+\s+"([^"]+)")raw");
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            auto match = trackRegex.match(line);
            if (match.hasMatch()) {
                QFileInfo refFi(gdiFi.dir(), match.captured(1));
                files.append(refFi.absoluteFilePath());
            }
        }
    }
    return files;
}

} // namespace LudoShelf::Scanning

