#include <QtTest/QtTest>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include "../src/launch/LaunchService.h"

class TestLaunchService : public QObject {
    Q_OBJECT
private slots:
    void testPlaceholderExpansion() {
        QTemporaryFile tempFile(QDir::tempPath() + "/XXXXXX.nes");
        QVERIFY(tempFile.open());
        QString tempFilePath = tempFile.fileName();

        LudoShelf::Domain::Game game;
        game.title = "Super Mario Bros";

        LudoShelf::Domain::GameFile file;
        file.path = tempFilePath;

        LudoShelf::Domain::System system;
        system.name = "Nintendo NES";
        system.shortName = "nes";

        LudoShelf::Domain::EmulatorProfile emulator;
        emulator.program = "retroarch";

        LudoShelf::Domain::ArgumentTemplate arg1;
        arg1.templateString = "-L";

        LudoShelf::Domain::ArgumentTemplate arg2;
        arg2.templateString = "/cores/mesen.so";

        LudoShelf::Domain::ArgumentTemplate arg3;
        arg3.templateString = "{game.path}";

        emulator.arguments << arg1 << arg2 << arg3;

        auto cmd = LudoShelf::Launch::LaunchService::prepareCommand(game, file, system, emulator);
        QVERIFY(cmd.valid);
        QCOMPARE(cmd.program, QString("retroarch"));
        QCOMPARE(cmd.arguments.size(), 3);
        QCOMPARE(cmd.arguments[2], tempFilePath);
    }

    void respectsSavedArgumentPositions() {
        QTemporaryFile tempFile(QDir::tempPath() + "/XXXXXX.nes");
        QVERIFY(tempFile.open());
        LudoShelf::Domain::Game game;
        LudoShelf::Domain::GameFile file;
        file.path = tempFile.fileName();
        LudoShelf::Domain::System system;
        LudoShelf::Domain::EmulatorProfile emulator;
        emulator.program = "retroarch";
        emulator.arguments = {
            {QUuid::createUuid(), 2, "{game.path}", false},
            {QUuid::createUuid(), 0, "-L", false},
            {QUuid::createUuid(), 1, "/cores/mesen.so", false},
        };

        const auto cmd = LudoShelf::Launch::LaunchService::prepareCommand(game, file, system, emulator);
        QCOMPARE(cmd.arguments, QStringList({"-L", "/cores/mesen.so", file.path}));
    }

    void expandsOrderedCommandFragmentsWithoutSplittingGamePaths() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        QVERIFY(QDir().mkpath(temporary.filePath("folder with spaces")));
        const QString gamePath = temporary.filePath("folder with spaces/game.nes");
        QFile gameFile(gamePath);
        QVERIFY(gameFile.open(QIODevice::WriteOnly));
        gameFile.close();
        LudoShelf::Domain::Game game;
        LudoShelf::Domain::GameFile file;
        file.path = gamePath;
        LudoShelf::Domain::System system;
        LudoShelf::Domain::EmulatorProfile emulator;
        emulator.program = "mednafen";
        emulator.arguments = {
            {QUuid::createUuid(), 0, "-force_module md", false},
            {QUuid::createUuid(), 1, "-video.fs 1", false},
            {QUuid::createUuid(), 2, "{game.path}", false},
        };

        const auto cmd = LudoShelf::Launch::LaunchService::prepareCommand(game, file, system, emulator);
        QCOMPARE(cmd.arguments, QStringList({"-force_module", "md", "-video.fs", "1", file.path}));
    }

    void shellModeQuotesRomDerivedArguments() {
#ifdef Q_OS_WIN
        QSKIP("This regression test exercises POSIX shell metacharacters.");
#else
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString marker = temporary.filePath("must-not-exist");

        LudoShelf::Domain::Game game;
        game.title = QStringLiteral("safe; touch %1").arg(marker);
        LudoShelf::Domain::GameFile file;
        LudoShelf::Domain::System system;
        LudoShelf::Domain::EmulatorProfile emulator;
        emulator.program = "printf";
        emulator.shellMode = true;
        emulator.arguments = {{QUuid::createUuid(), 0, "{game.title}", false}};

        LudoShelf::Launch::LaunchService service;
        QSignalSpy finished(&service, &LudoShelf::Launch::LaunchService::gameFinished);
        QVERIFY(service.launchGame(game, file, system, emulator));
        QVERIFY(finished.wait(3000));
        QVERIFY(!QFile::exists(marker));
#endif
    }

    void rejectsDisabledProfileAndUnavailableRom() {
        LudoShelf::Domain::Game game;
        LudoShelf::Domain::GameFile file;
        file.path = QStringLiteral("/definitely/not/present.rom");
        LudoShelf::Domain::System system;
        LudoShelf::Domain::EmulatorProfile emulator;
        emulator.program = "retroarch";
        emulator.enabled = false;
        auto command = LudoShelf::Launch::LaunchService::prepareCommand(game, file, system, emulator);
        QVERIFY(!command.valid);
        QCOMPARE(command.validationError, QString("Emulator profile is disabled."));

        emulator.enabled = true;
        command = LudoShelf::Launch::LaunchService::prepareCommand(game, file, system, emulator);
        QVERIFY(!command.valid);
        QVERIFY(command.validationError.contains("unavailable"));
    }

};

QTEST_MAIN(TestLaunchService)
#include "test_launch_service.moc"
