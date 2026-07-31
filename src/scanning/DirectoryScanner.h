#ifndef LUDOSHELF_SCANNING_DIRECTORYSCANNER_H
#define LUDOSHELF_SCANNING_DIRECTORYSCANNER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QList>
#include <QUuid>

#include "../domain/Game.h"
#include "../domain/GameFile.h"

namespace LudoShelf::Scanning {

struct ScanCandidate {
    Domain::Game game;
    Domain::GameFile file;
};

struct ScanOptions {
    QStringList allowedExtensions;
    QStringList excludedExtensions;
    QStringList excludedPatterns;
    bool recursive{true};
    bool followSymlinks{false};
};

class DirectoryScanner : public QObject {
    Q_OBJECT
public:
    explicit DirectoryScanner(QObject *parent = nullptr);

    QList<ScanCandidate> scanDirectory(
        const QUuid& systemId,
        const QString& dirPath,
        const QStringList& allowedExtensions,
        bool recursive = true
    );
    QList<ScanCandidate> scanDirectory(const QUuid& systemId, const QString& dirPath,
                                       const ScanOptions& options);
};

} // namespace LudoShelf::Scanning

#endif // LUDOSHELF_SCANNING_DIRECTORYSCANNER_H
