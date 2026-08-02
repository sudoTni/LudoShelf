#include "SystemsView.h"

#include <QLabel>
#include <QItemSelectionModel>
#include <QMenu>

namespace LudoShelf::UI {

SystemsView::SystemsView(QWidget *parent)
    : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    auto *headerLabel = new QLabel("SYSTEMS", this);
    headerLabel->setStyleSheet("font-weight: bold; color: #888; letter-spacing: 1px;");
    layout->addWidget(headerLabel);

    m_listView = new QListView(this);
    m_listView->setAccessibleName("Systems list");
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listView->setAlternatingRowColors(true);
    m_listView->setIconSize(QSize(28, 28));
    m_listView->setContextMenuPolicy(Qt::CustomContextMenu);
    layout->addWidget(m_listView);

    m_addSystemBtn = new QPushButton("+ Add System", this);
    m_addSystemBtn->setAccessibleName("Add system");
    layout->addWidget(m_addSystemBtn);

    connect(m_addSystemBtn, &QPushButton::clicked, this, &SystemsView::addSystemRequested);
    connect(m_listView, &QListView::customContextMenuRequested, this, &SystemsView::showContextMenu);
}

void SystemsView::setModel(Models::SystemListModel *model) {
    m_model = model;
    m_listView->setModel(model);

    connect(m_listView->selectionModel(), &QItemSelectionModel::currentChanged,
            this, [this](const QModelIndex &current, const QModelIndex &previous) {
        Q_UNUSED(previous);
        if (current.isValid() && m_model) {
            QUuid sysId = QUuid::fromString(m_model->data(current, Models::SystemListModel::SystemIdRole).toString());
            emit systemSelected(sysId);
        }
    });
}

void SystemsView::showContextMenu(const QPoint& pos) {
    QModelIndex idx = m_listView->indexAt(pos);
    if (!idx.isValid() || !m_model) return;

    QUuid sysId = QUuid::fromString(m_model->data(idx, Models::SystemListModel::SystemIdRole).toString());

    QMenu menu(this);
    menu.addAction("Edit System Properties...", this, [this, sysId]() {
        emit editSystemRequested(sysId);
    });
    menu.addAction("Configure Emulator Profile...", this, [this, sysId]() {
        emit editEmulatorRequested(sysId);
    });
    menu.addAction("Rescan ROM Folders", this, [this, sysId]() {
        emit rescanSystemRequested(sysId);
    });
    menu.addSeparator();
    menu.addAction("Delete System", this, [this, sysId]() {
        emit deleteSystemRequested(sysId);
    });

    menu.exec(m_listView->mapToGlobal(pos));
}

} // namespace LudoShelf::UI
