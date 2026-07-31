#include "PlaceholderGenerator.h"
#include "MediaStorageManager.h"

#include <QPainter>
#include <QBuffer>
#include <QCryptographicHash>

namespace LudoShelf::Media {

QImage PlaceholderGenerator::generatePlaceholderImage(
    const QString& gameTitle,
    const QString& systemName,
    int width,
    int height
) {
    QImage img(width, height, QImage::Format_RGB32);

    // Deterministic background color derived from title hash
    QByteArray hash = QCryptographicHash::hash(gameTitle.toUtf8(), QCryptographicHash::Md5);
    int r = static_cast<unsigned char>(hash.at(0)) % 120 + 20;
    int g = static_cast<unsigned char>(hash.at(1)) % 120 + 20;
    int b = static_cast<unsigned char>(hash.at(2)) % 120 + 20;
    img.fill(QColor(r, g, b));

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);

    // Draw decorative border
    p.setPen(QPen(QColor(255, 255, 255, 120), 4));
    p.drawRect(img.rect().adjusted(8, 8, -8, -8));

    // Draw System Header
    p.setPen(QColor(255, 255, 255, 200));
    p.setFont(QFont("sans-serif", 10, QFont::Bold));
    p.drawText(QRect(12, 16, width - 24, 24), Qt::AlignCenter, systemName.toUpper());

    // Draw Game Title
    p.setPen(Qt::white);
    p.setFont(QFont("sans-serif", 14, QFont::Bold));
    p.drawText(QRect(16, 60, width - 32, height - 120), Qt::AlignCenter | Qt::TextWordWrap, gameTitle);

    // Draw Placeholder Badge
    p.setPen(QColor(255, 255, 255, 160));
    p.setFont(QFont("sans-serif", 8, QFont::Normal));
    p.drawText(QRect(12, height - 32, width - 24, 20), Qt::AlignCenter, "[ PLACEHOLDER ART ]");

    p.end();
    return img;
}

QString PlaceholderGenerator::generateAndStorePlaceholder(
    const QUuid& gameId,
    const QString& gameTitle,
    const QString& systemName
) {
    QImage img = generatePlaceholderImage(gameTitle, systemName);

    QByteArray data;
    QBuffer buf(&data);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");

    return MediaStorageManager::instance().storeOriginalImage(
        gameId,
        data,
        "placeholder",
        "image/png",
        "procedural"
    );
}

} // namespace LudoShelf::Media
