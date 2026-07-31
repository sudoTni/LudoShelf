#ifndef LUDOSHELF_LAUNCH_LAUNCHSERVICE_H
#define LUDOSHELF_LAUNCH_LAUNCHSERVICE_H

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QUuid>

#include "../domain/Game.h"
#include "../domain/GameFile.h"
#include "../domain/System.h"
#include "../domain/EmulatorProfile.h"

namespace LudoShelf::Launch {

struct LaunchCommand {
    QString program;
    QStringList arguments;
    QString workingDirectory;
    QMap<QString, QString> environment;
    bool shellMode{false};
    bool valid{true};
    QString validationError;
};

class LaunchService : public QObject {
    Q_OBJECT
public:
    explicit LaunchService(QObject *parent = nullptr);

    static LaunchCommand prepareCommand(
        const Domain::Game& game,
        const Domain::GameFile& file,
        const Domain::System& system,
        const Domain::EmulatorProfile& emulator
    );

    bool launchGame(
        const Domain::Game& game,
        const Domain::GameFile& file,
        const Domain::System& system,
        const Domain::EmulatorProfile& emulator
    );

signals:
    void gameStarted(const QUuid& gameId, qint64 pid);
    void gameFinished(const QUuid& gameId, int exitCode, QProcess::ExitStatus exitStatus, int durationSeconds);
    void frontendVisibilityRequested(Domain::HidePolicy policy, bool restore);
    void launchFailed(const QUuid& gameId, const QString& error);

private:
    static QString expandTemplate(
        const QString& argTemplate,
        const Domain::Game& game,
        const Domain::GameFile& file,
        const Domain::System& system,
        const Domain::EmulatorProfile& emulator
    );
};

} // namespace LudoShelf::Launch

#endif // LUDOSHELF_LAUNCH_LAUNCHSERVICE_H
