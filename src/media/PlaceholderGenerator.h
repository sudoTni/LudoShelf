#ifndef LUDOSHELF_MEDIA_PLACEHOLDERGENERATOR_H
#define LUDOSHELF_MEDIA_PLACEHOLDERGENERATOR_H

#include <QImage>
#include <QString>
#include <QUuid>

namespace LudoShelf::Media {

class PlaceholderGenerator {
public:
    static QImage generatePlaceholderImage(
        const QString& gameTitle,
        const QString& systemName,
        int width = 300,
        int height = 450
    );

    static QString generateAndStorePlaceholder(
        const QUuid& gameId,
        const QString& gameTitle,
        const QString& systemName
    );
};

} // namespace LudoShelf::Media

#endif // LUDOSHELF_MEDIA_PLACEHOLDERGENERATOR_H
