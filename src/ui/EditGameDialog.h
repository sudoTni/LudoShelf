#ifndef LUDOSHELF_UI_EDITGAMEDIALOG_H
#define LUDOSHELF_UI_EDITGAMEDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QDateEdit>
#include <QTextEdit>

#include "../domain/Game.h"

namespace LudoShelf::UI {

class EditGameDialog : public QDialog {
    Q_OBJECT
public:
    explicit EditGameDialog(const Domain::Game& game, QWidget *parent = nullptr);

    Domain::Game updatedGame() const { return m_game; }

protected:
    void accept() override;

private:
    QLineEdit *m_titleEdit;
    QLineEdit *m_sortTitleEdit;
    QLineEdit *m_developerEdit;
    QLineEdit *m_publisherEdit;
    QLineEdit *m_regionEdit;
    QDateEdit *m_releaseDateEdit;
    QCheckBox *m_favoriteCheck;
    QTextEdit *m_descriptionText;

    Domain::Game m_game;
};

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_EDITGAMEDIALOG_H
