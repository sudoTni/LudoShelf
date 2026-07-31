#include <QtTest/QtTest>
#include "../src/scanning/FilenameParser.h"

class TestFilenameParser : public QObject {
    Q_OBJECT
private slots:
    void testStandardFilename() {
        auto parsed = LudoShelf::Scanning::FilenameParser::parse("Super Example (USA) (Rev 1) [!].nes");
        QCOMPARE(parsed.cleanTitle, QString("Super Example"));
        QCOMPARE(parsed.region, QString("USA"));
        QCOMPARE(parsed.revision, QString("Rev 1"));
        QCOMPARE(parsed.dumpStatus, QString("Verified"));
        QCOMPARE(parsed.extension, QString("nes"));
    }

    void testJapaneseFilename() {
        auto parsed = LudoShelf::Scanning::FilenameParser::parse("例のゲーム (Japan).cue");
        QCOMPARE(parsed.cleanTitle, QString("例のゲーム"));
        QCOMPARE(parsed.region, QString("Japan"));
        QCOMPARE(parsed.extension, QString("cue"));
    }

    void testMetacharactersFilename() {
        auto parsed = LudoShelf::Scanning::FilenameParser::parse("Game;Name$(Test).zip");
        QCOMPARE(parsed.cleanTitle, QString("Game;Name$(Test)"));
        QCOMPARE(parsed.extension, QString("zip"));
    }

    void parsesGoodGenFilename() {
        const auto parsed = LudoShelf::Scanning::FilenameParser::parse("Altered Beast (REV 02) (JU) [!].zip");
        QCOMPARE(parsed.cleanTitle, QString("Altered Beast"));
        QCOMPARE(parsed.region, QString("Japan"));
        QCOMPARE(parsed.revision, QString("REV 02"));
        QCOMPARE(parsed.dumpStatus, QString("Verified"));
    }
};

QTEST_MAIN(TestFilenameParser)
#include "test_filename_parser.moc"
