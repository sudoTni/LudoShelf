#include "LaunchService.h"

#include <QFileInfo>
#include <QDir>
#include <QProcessEnvironment>
#include <QDateTime>
#include <QDebug>
#include <QTimer>

#include <algorithm>

namespace LudoShelf::Launch {
namespace {

QString shellQuote(const QString& value) {
#ifdef Q_OS_WIN
    // cmd.exe treats double-quoted arguments as a single token.  Escaping the
    // quote itself keeps a ROM path or title from ending that quoted token.
    QString escaped = value;
    escaped.replace('"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
#else
    // POSIX shells cannot interpret any metacharacter inside single quotes.
    // The conventional quote-close/escaped-quote/reopen sequence preserves a
    // literal apostrophe too.
    QString escaped = value;
    escaped.replace('\'', QStringLiteral("'\"'\"'"));
    return QStringLiteral("'%1'").arg(escaped);
#endif
}

} // namespace

LaunchService::LaunchService(QObject *parent)
    : QObject(parent) {}

QString LaunchService::expandTemplate(
    const QString& argTemplate,
    const Domain::Game& game,
    const Domain::GameFile& file,
    const Domain::System& system,
    const Domain::EmulatorProfile& emulator
) {
    QFileInfo fi(file.path);
    QString result = argTemplate;

    result.replace("{game.path}", file.path);
    result.replace("{game.dir}", fi.absolutePath());
    result.replace("{game.filename}", fi.fileName());
    result.replace("{game.stem}", fi.completeBaseName());
    result.replace("{game.extension}", fi.suffix().toLower());
    result.replace("{game.title}", game.title);
    result.replace("{game.id}", game.id.toString(QUuid::WithBraces));
    result.replace("{game.region}", game.region);
    result.replace("{game.developer}", game.developer);

    result.replace("{system.id}", system.id.toString(QUuid::WithBraces));
    result.replace("{system.name}", system.name);
    result.replace("{system.short_name}", system.shortName);
    result.replace("{emulator.name}", emulator.name);
    result.replace("{emulator.program}", emulator.program);

    return result;
}

LaunchCommand LaunchService::prepareCommand(
    const Domain::Game& game,
    const Domain::GameFile& file,
    const Domain::System& system,
    const Domain::EmulatorProfile& emulator
) {
    LaunchCommand cmd;
    cmd.program = emulator.program;
    cmd.workingDirectory = emulator.workingDirectory;
    cmd.environment = emulator.environment;
    cmd.shellMode = emulator.shellMode;

    if (cmd.program.isEmpty()) {
        cmd.valid = false;
        cmd.validationError = "Emulator executable path is empty.";
        return cmd;
    }

    if (!file.path.isEmpty() && !QFileInfo::exists(file.path)) {
        cmd.valid = false;
        cmd.validationError = QString("Game file does not exist: %1").arg(file.path);
        return cmd;
    }

    QStringList launcherArguments;
    if (emulator.launchType == Domain::LaunchType::Flatpak) {
        launcherArguments = {QStringLiteral("run"), cmd.program};
        cmd.program = QStringLiteral("flatpak");
    } else if (emulator.launchType == Domain::LaunchType::Wine) {
        launcherArguments = {cmd.program};
        cmd.program = QStringLiteral("wine");
    }

    // Default arguments if profile has no custom arguments specified
    QStringList gameArguments;
    if (emulator.arguments.isEmpty()) {
        gameArguments.append(file.path);
    } else {
        QList<Domain::ArgumentTemplate> orderedArguments = emulator.arguments;
        std::stable_sort(orderedArguments.begin(), orderedArguments.end(), [](const auto& left, const auto& right) {
            return left.position < right.position;
        });
        for (const auto& argTpl : orderedArguments) {
            // A line in the profile is an ordered command fragment.  Split it
            // before expansion so {game.path} remains one argument even when
            // its resolved filesystem path contains spaces.
            const QString expandedLine = expandTemplate(argTpl.templateString, game, file, system, emulator);
            if (argTpl.optional && expandedLine.trimmed().isEmpty()) continue;
            const QStringList fragments = QProcess::splitCommand(argTpl.templateString);
            for (const QString& fragment : fragments) {
                const QString expanded = expandTemplate(fragment, game, file, system, emulator);
                if (!expanded.isEmpty()) gameArguments.append(expanded);
            }
        }
    }
    cmd.arguments = launcherArguments;
    cmd.arguments.append(gameArguments);

    return cmd;
}

bool LaunchService::launchGame(
    const Domain::Game& game,
    const Domain::GameFile& file,
    const Domain::System& system,
    const Domain::EmulatorProfile& emulator
) {
    LaunchCommand cmd = prepareCommand(game, file, system, emulator);

    if (!cmd.valid) {
        emit launchFailed(game.id, cmd.validationError);
        return false;
    }

    auto *process = new QProcess(this);
    if (!cmd.workingDirectory.isEmpty()) {
        process->setWorkingDirectory(cmd.workingDirectory);
    }

    if (!cmd.environment.isEmpty()) {
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        for (auto it = cmd.environment.cbegin(); it != cmd.environment.cend(); ++it) {
            env.insert(it.key(), it.value());
        }
        process->setProcessEnvironment(env);
    }
    if (!emulator.captureOutput) process->setProcessChannelMode(QProcess::ForwardedChannels);

    QUuid gameId = game.id;
    QDateTime startTime = QDateTime::currentDateTimeUtc();

    const Domain::HidePolicy hidePolicy = emulator.hidePolicy;
    connect(process, &QProcess::started, this, [this, process, gameId, hidePolicy]() {
        emit gameStarted(gameId, process->processId());
        emit frontendVisibilityRequested(hidePolicy, false);
    });

    connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, process, gameId, startTime, hidePolicy](int exitCode, QProcess::ExitStatus exitStatus) {
        int duration = static_cast<int>(startTime.secsTo(QDateTime::currentDateTimeUtc()));
        emit gameFinished(gameId, exitCode, exitStatus, duration);
        emit frontendVisibilityRequested(hidePolicy, true);
        process->deleteLater();
    });

    connect(process, &QProcess::errorOccurred, this, [this, process, gameId](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            emit launchFailed(gameId, QString("Failed to start emulator: %1").arg(process->errorString()));
            process->deleteLater();
        }
    });

    if (emulator.detach) {
        if (cmd.shellMode) {
            QStringList shellTokens;
            shellTokens.reserve(cmd.arguments.size() + 1);
            shellTokens.append(shellQuote(cmd.program));
            for (const QString& argument : cmd.arguments) shellTokens.append(shellQuote(argument));
#ifdef Q_OS_WIN
            process->setProgram(QStringLiteral("cmd.exe"));
            process->setArguments({QStringLiteral("/C"), shellTokens.join(' ')});
#else
            process->setProgram(QStringLiteral("/bin/sh"));
            process->setArguments({QStringLiteral("-c"), shellTokens.join(' ')});
#endif
        } else {
            process->setProgram(cmd.program);
            process->setArguments(cmd.arguments);
        }
        qint64 pid = 0;
        if (!process->startDetached(&pid)) {
            emit launchFailed(gameId, QString("Failed to detach emulator: %1").arg(process->errorString()));
            process->deleteLater();
            return false;
        }
        // Detached processes cannot be observed after this point.  Count a
        // successful spawn as a completed launch rather than leaving a game
        // permanently marked Playing until the next application start.
        emit gameStarted(gameId, pid);
        QTimer::singleShot(0, this, [this, process, gameId] {
            emit gameFinished(gameId, 0, QProcess::NormalExit, 0);
            process->deleteLater();
        });
        return true;
    }

    if (cmd.shellMode) {
        QStringList shellTokens;
        shellTokens.reserve(cmd.arguments.size() + 1);
        shellTokens.append(shellQuote(cmd.program));
        for (const QString& argument : cmd.arguments) shellTokens.append(shellQuote(argument));
        const QString fullCmd = shellTokens.join(' ');
#ifdef Q_OS_WIN
        process->start("cmd.exe", QStringList() << "/C" << fullCmd);
#else
        process->start("/bin/sh", QStringList() << "-c" << fullCmd);
#endif
    } else {
        process->start(cmd.program, cmd.arguments);
    }

    return true;
}

} // namespace LudoShelf::Launch
