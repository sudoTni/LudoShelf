#ifndef LUDOSHELF_UI_ARTWORKDELEGATE_H
#define LUDOSHELF_UI_ARTWORKDELEGATE_H

#include <QStyledItemDelegate>

namespace LudoShelf::UI {

class ArtworkDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    enum class CoverAspect { Portrait, Landscape };

    explicit ArtworkDelegate(QObject *parent = nullptr);

    void setCoverAspect(CoverAspect aspect);
    CoverAspect coverAspect() const;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    CoverAspect m_coverAspect{CoverAspect::Portrait};
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_ARTWORKDELEGATE_H
