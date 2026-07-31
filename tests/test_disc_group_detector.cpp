#include <QtTest/QtTest>
#include "../src/scanning/DiscGroupDetector.h"

class TestDiscGroupDetector : public QObject {
    Q_OBJECT
private slots:
    void testDiscNumberExtraction() {
        auto info1 = LudoShelf::Scanning::DiscGroupDetector::analyzeFile("Disc Game (Disc 1).chd");
        QCOMPARE(info1.discNumber, 1);

        auto info2 = LudoShelf::Scanning::DiscGroupDetector::analyzeFile("Disc Game (Disc 2).chd");
        QCOMPARE(info2.discNumber, 2);
    }
};

QTEST_MAIN(TestDiscGroupDetector)
#include "test_disc_group_detector.moc"
