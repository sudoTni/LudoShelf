#include "ArtworkDelegate.h"
#include "../models/GameTableModel.h"

#include <QPainter>
#include <QApplication>
#include <QPixmap>

namespace LudoShelf::UI {

ArtworkDelegate::ArtworkDelegate(QObject *parent)
    : QStyledItemDelegate(parent) {}

void ArtworkDelegate::setCoverAspect(CoverAspect aspect) {
    m_coverAspect = aspect;
}

ArtworkDelegate::CoverAspect ArtworkDelegate::coverAspect() const {
    return m_coverAspect;
}

QSize ArtworkDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    return m_coverAspect == CoverAspect::Landscape ? QSize(300, 220) : QSize(160, 220);
}

void ArtworkDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    QRect rect = option.rect.adjusted(6, 6, -6, -6);

    bool selected = option.state & QStyle::State_Selected;

    // Card Background
    QColor cardBg = selected ? QColor(43, 120, 228, 40) : QColor(40, 40, 40);
    QColor borderColor = selected ? QColor(43, 120, 228) : QColor(60, 60, 60);

    painter->setBrush(cardBg);
    painter->setPen(QPen(borderColor, selected ? 2 : 1));
    painter->drawRoundedRect(rect, 8, 8);

    const bool landscape = m_coverAspect == CoverAspect::Landscape;
    const int artHeight = landscape ? 164 : 150;

    // Cover Art Box Area.  Landscape cards are deliberately wider, so fewer
    // games fit on a row and wide box art has room to retain its proportions.
    QRect artRect(rect.left() + 8, rect.top() + 8, rect.width() - 16, artHeight);
    painter->setBrush(QColor(25, 25, 25));
    painter->setPen(QPen(QColor(50, 50, 50), 1));
    painter->drawRoundedRect(artRect, 6, 6);

    // Title label text & Cover Pixmap
    QModelIndex titleIdx = index.model()->index(index.row(), Models::GameTableModel::ColumnTitle, index.parent());
    QString title = index.model()->data(titleIdx).toString();

    QVariant coverVal = index.model()->data(titleIdx, Models::GameTableModel::CoverPixmapRole);
    bool drewImage = false;
    if (coverVal.canConvert<QPixmap>()) {
        QPixmap pix = coverVal.value<QPixmap>();
        if (!pix.isNull()) {
            painter->setClipRect(artRect.adjusted(1, 1, -1, -1));
            // Never crop cover artwork.  A portrait card has room to pillarbox
            // a taller 2:3 box art image, while a landscape card can letterbox
            // wide artwork; both keep the source proportions intact.
            const QPixmap scaled = pix.scaled(artRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QRect imageRect(QPoint(artRect.center().x() - scaled.width() / 2,
                                         artRect.center().y() - scaled.height() / 2), scaled.size());
            painter->drawPixmap(imageRect.topLeft(), scaled);
            painter->setClipping(false);
            drewImage = true;
        }
    }

    if (!drewImage) {
        // Draw Title text fallback inside box if image failed
        painter->setPen(QColor(180, 180, 180));
        painter->setFont(QFont("sans-serif", 10, QFont::Bold));
        painter->drawText(artRect.adjusted(8, 8, -8, -8), Qt::AlignCenter | Qt::TextWordWrap, title);
    }

    QModelIndex favIdx = index.model()->index(index.row(), Models::GameTableModel::ColumnFavorite, index.parent());
    bool isFav = !index.model()->data(favIdx).toString().isEmpty();

    // Draw Title below artwork box
    QRect textRect(rect.left() + 8, artRect.bottom() + 8, rect.width() - 16, rect.bottom() - artRect.bottom() - 12);
    painter->setPen(selected ? QColor(255, 255, 255) : QColor(220, 220, 220));
    painter->setFont(QFont("sans-serif", 9, QFont::DemiBold));
    painter->drawText(textRect, Qt::AlignTop | Qt::AlignLeft | Qt::TextWordWrap, title);

    // Draw Favorite star badge
    if (isFav) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(255, 193, 7));
        painter->drawEllipse(rect.right() - 20, rect.top() + 12, 14, 14);

        painter->setPen(QColor(0, 0, 0));
        painter->setFont(QFont("sans-serif", 8, QFont::Bold));
        painter->drawText(QRect(rect.right() - 20, rect.top() + 12, 14, 14), Qt::AlignCenter, "★");
    }

    painter->restore();
}

} // namespace LudoShelf::UI
