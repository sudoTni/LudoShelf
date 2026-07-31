#include <QApplication>
#include <QDebug>
#include <QIcon>
#include "database/DatabaseManager.h"
#include "ui/MainWindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("LudoShelf");
    app.setOrganizationName("LudoShelf");
    app.setApplicationVersion("0.1.0");
    app.setWindowIcon(QIcon(":/ludo.png"));

    // Initialize Database
    if (!LudoShelf::Database::DatabaseManager::instance().initialize()) {
        qCritical() << "Failed to initialize database!";
        return 1;
    }

    LudoShelf::UI::MainWindow mainWindow;
    mainWindow.show();

    int result = app.exec();

    LudoShelf::Database::DatabaseManager::instance().close();
    return result;
}
