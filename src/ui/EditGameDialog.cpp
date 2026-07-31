#include "EditGameDialog.h"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMessageBox>
#include <QLabel>

namespace LudoShelf::UI {

EditGameDialog::EditGameDialog(const Domain::Game& game, QWidget *parent)
    : QDialog(parent)
    , m_game(game)
{
    setWindowTitle("Edit Game Details");
    resize(550, 480);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout();

    m_titleEdit = new QLineEdit(game.title, this);
    form->addRow("Title*:", m_titleEdit);

    m_sortTitleEdit = new QLineEdit(game.sortTitle, this);
    form->addRow("Sort Title:", m_sortTitleEdit);

    m_developerEdit = new QLineEdit(game.developer, this);
    form->addRow("Developer:", m_developerEdit);

    m_publisherEdit = new QLineEdit(game.publisher, this);
    form->addRow("Publisher:", m_publisherEdit);

    m_regionEdit = new QLineEdit(game.region, this);
    form->addRow("Region:", m_regionEdit);

    m_releaseDateEdit = new QDateEdit(game.releaseDate.isValid() ? game.releaseDate : QDate(1990, 1, 1), this);
    m_releaseDateEdit->setCalendarPopup(true);
    form->addRow("Release Date:", m_releaseDateEdit);

    auto *statusLabel = new QLabel("Tracked automatically: Unplayed, Playing while the emulator runs, then Played.", this);
    statusLabel->setWordWrap(true);
    form->addRow("Status:", statusLabel);

    m_favoriteCheck = new QCheckBox("Favorite Game", this);
    m_favoriteCheck->setChecked(game.favorite);
    form->addRow("", m_favoriteCheck);

    m_descriptionText = new QTextEdit(game.description, this);
    m_descriptionText->setMaximumHeight(100);
    form->addRow("Description:", m_descriptionText);

    layout->addLayout(form);

    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();

    auto *saveBtn = new QPushButton("Save Changes", this);
    saveBtn->setStyleSheet("padding: 6px 14px; font-weight: bold; background-color: #2b78e4; color: white;");
    btnLayout->addWidget(saveBtn);

    auto *cancelBtn = new QPushButton("Cancel", this);
    btnLayout->addWidget(cancelBtn);

    layout->addLayout(btnLayout);

    connect(saveBtn, &QPushButton::clicked, this, &EditGameDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

void EditGameDialog::accept() {
    if (m_titleEdit->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, "Validation Error", "Title cannot be empty.");
        return;
    }

    m_game.title = m_titleEdit->text().trimmed();
    m_game.sortTitle = m_sortTitleEdit->text().trimmed();
    m_game.developer = m_developerEdit->text().trimmed();
    m_game.publisher = m_publisherEdit->text().trimmed();
    m_game.region = m_regionEdit->text().trimmed();
    m_game.releaseDate = m_releaseDateEdit->date();
    m_game.favorite = m_favoriteCheck->isChecked();
    m_game.description = m_descriptionText->toPlainText().trimmed();

    QDialog::accept();
}

} // namespace LudoShelf::UI
