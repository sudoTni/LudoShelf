#ifndef LUDOSHELF_UI_SYSTEMSVIEW_H
#define LUDOSHELF_UI_SYSTEMSVIEW_H

#include <QWidget>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>
#include <QUuid>

#include "../models/SystemListModel.h"

namespace LudoShelf::UI {

class SystemsView : public QWidget {
    Q_OBJECT
public:
    explicit SystemsView(QWidget *parent = nullptr);

    void setModel(Models::SystemListModel *model);

signals:
    void systemSelected(const QUuid& systemId);
    void addSystemRequested();
    void editSystemRequested(const QUuid& systemId);
    void editEmulatorRequested(const QUuid& systemId);
    void rescanSystemRequested(const QUuid& systemId);
    void deleteSystemRequested(const QUuid& systemId);

private slots:
    void showContextMenu(const QPoint& pos);

private:
    QListView *m_listView;
    QPushButton *m_addSystemBtn;
    Models::SystemListModel *m_model{nullptr};
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_SYSTEMSVIEW_H
