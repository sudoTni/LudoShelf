#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>

#include <algorithm>

#include <archive.h>
#include <archive_entry.h>

#include "../src/metadata/LibretroDatabaseProvider.h"
#include "../src/app/AppPaths.h"
#include "../src/metadata/RomHashService.h"

namespace {
void appendString(QByteArray& data, const QByteArray& value) {
    if (value.size() < 32) data.append(static_cast<char>(0xa0 | value.size()));
    else { data.append(char(0xd9)); data.append(static_cast<char>(value.size())); }
    data.append(value);
}
void appendBinary(QByteArray& data, const QByteArray& value) {
    data.append(char(0xc4)); data.append(static_cast<char>(value.size())); data.append(value);
}
void appendInteger(QByteArray& data, int value) {
    if (value < 128) data.append(static_cast<char>(value));
    else { data.append(char(0xcd)); data.append(static_cast<char>((value >> 8) & 0xff)); data.append(static_cast<char>(value & 0xff)); }
}
void appendField(QByteArray& data, const QByteArray& key, const QByteArray& value) {
    appendString(data, key); appendString(data, value);
}
QString writeFixtureRdb(const QTemporaryDir& temporary) {
    QByteArray record;
    record.append(char(0x8b));
    appendField(record, "name", "Example Game");
    appendField(record, "description", "A carefully curated example description.");
    appendString(record, "crc"); appendBinary(record, "CBF43926");
    appendString(record, "size"); appendInteger(record, 9);
    appendString(record, "releaseyear"); appendInteger(record, 1991);
    appendField(record, "developer", "Example Studio");
    appendField(record, "publisher", "Example Publisher");
    appendField(record, "genre", "Platformer");
    appendField(record, "users", "1-2");
    appendField(record, "region", "USA");
    appendField(record, "language", "English");
    record.append(char(0xc0));

    QByteArray database("RARCHDB\0", 8);
    const quint64 offset = static_cast<quint64>(16 + record.size());
    for (int shift = 56; shift >= 0; shift -= 8) database.append(static_cast<char>((offset >> shift) & 0xff));
    database.append(record);
    database.append(char(0x81)); appendString(database, "count"); appendInteger(database, 1);
    QFile file(temporary.filePath("Nintendo - Nintendo Entertainment System.rdb"));
    if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(database); file.close();
    return temporary.path();
}

QString writeArchiveWithMember(const QTemporaryDir& temporary, const QString& memberName) {
    const QString archivePath = temporary.filePath("fixture.zip");
    archive* writer = archive_write_new();
    if (!writer || archive_write_set_format_zip(writer) != ARCHIVE_OK ||
        archive_write_open_filename(writer, archivePath.toUtf8().constData()) != ARCHIVE_OK) {
        if (writer) archive_write_free(writer);
        return {};
    }
    archive_entry* entry = archive_entry_new();
    const QByteArray data("transformed-disc");
    archive_entry_set_pathname(entry, memberName.toUtf8().constData());
    archive_entry_set_filetype(entry, AE_IFREG);
    archive_entry_set_perm(entry, 0644);
    archive_entry_set_size(entry, data.size());
    const bool success = archive_write_header(writer, entry) == ARCHIVE_OK &&
        archive_write_data(writer, data.constData(), static_cast<size_t>(data.size())) == data.size();
    archive_entry_free(entry);
    archive_write_close(writer);
    archive_write_free(writer);
    return success ? archivePath : QString();
}
}

class TestRomMetadata : public QObject {
    Q_OBJECT
private slots:
    void hashesPlainRomInOnePass() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString path = temporary.filePath("vector.bin");
        QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.write("123456789") == 9); file.close();
        const auto candidate = LudoShelf::Metadata::RomHashService::hashPlainFile(path, path);
        QCOMPARE(candidate.byteSize, 9);
        QCOMPARE(candidate.crc32, QString("CBF43926"));
        QCOMPARE(candidate.md5, QString("25F9E794323B453885F5181F1B624D0B"));
        QCOMPARE(candidate.sha1, QString("F7C3BC1D808E04732ADF679965CCC34CA7AE3441"));
        QCOMPARE(candidate.sha256, QString("15E2B0D3C33891EBB0F1EF609EC419420C20E320CE94C65FBC8C3312448EB225"));
    }

    void hashesCanonicalSmdPayload() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        QByteArray canonical(16 * 1024, Qt::Uninitialized);
        for (int index = 0; index < canonical.size(); ++index) canonical[index] = static_cast<char>(index & 0xff);
        const QString canonicalPath = temporary.filePath("canonical.bin");
        QFile canonicalFile(canonicalPath); QVERIFY(canonicalFile.open(QIODevice::WriteOnly)); canonicalFile.write(canonical); canonicalFile.close();
        const auto expected = LudoShelf::Metadata::RomHashService::hashPlainFile(canonicalPath, canonicalPath);

        QByteArray smd(512, '\0');
        QByteArray encoded(16 * 1024, Qt::Uninitialized);
        for (int index = 0; index < 8 * 1024; ++index) {
            encoded[index] = canonical[2 * index + 1];
            encoded[8 * 1024 + index] = canonical[2 * index];
        }
        smd.append(encoded);
        const QString smdPath = temporary.filePath("fixture.smd");
        QFile smdFile(smdPath); QVERIFY(smdFile.open(QIODevice::WriteOnly)); smdFile.write(smd); smdFile.close();
        const auto batch = LudoShelf::Metadata::RomHashService::discoverAndHash(smdPath, QFileInfo(smdPath).lastModified());
        const auto it = std::find_if(batch.candidates.cbegin(), batch.candidates.cend(), [](const auto& candidate) {
            return candidate.payloadKey.endsWith("::smd-deinterleaved");
        });
        QVERIFY(it != batch.candidates.cend());
        QCOMPARE(it->byteSize, expected.byteSize);
        QCOMPARE(it->crc32, expected.crc32);
        QCOMPARE(it->sha1, expected.sha1);
    }

    void hashesCanonicalINesPayloadWithoutHeaderOrTrainer() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QByteArray payload("123456789");
        const QString payloadPath = temporary.filePath("payload.bin");
        QFile payloadFile(payloadPath); QVERIFY(payloadFile.open(QIODevice::WriteOnly)); payloadFile.write(payload); payloadFile.close();
        const auto expected = LudoShelf::Metadata::RomHashService::hashPlainFile(payloadPath, payloadPath);

        QByteArray ines("NES\x1A", 4);
        ines.append(2, '\0');
        ines.append(char(0x04)); // trainer-present flag
        ines.append(9, '\0');
        QCOMPARE(ines.size(), 16);
        ines.append(512, '\0');
        ines.append(payload);
        const QString inesPath = temporary.filePath("fixture.nes");
        QFile inesFile(inesPath); QVERIFY(inesFile.open(QIODevice::WriteOnly)); inesFile.write(ines); inesFile.close();

        const auto batch = LudoShelf::Metadata::RomHashService::discoverAndHash(inesPath, QFileInfo(inesPath).lastModified());
        const auto it = std::find_if(batch.candidates.cbegin(), batch.candidates.cend(), [](const auto& candidate) {
            return candidate.payloadKey.endsWith("::ines-payload");
        });
        QVERIFY(it != batch.candidates.cend());
        QCOMPARE(it->byteSize, expected.byteSize);
        QCOMPARE(it->crc32, expected.crc32);
        QCOMPARE(it->sha1, expected.sha1);
    }

    void hashesOnlyDataTracksFromCueSheets() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString dataPath = temporary.filePath("data.bin");
        QFile data(dataPath); QVERIFY(data.open(QIODevice::WriteOnly)); data.write("disc data"); data.close();
        const auto expected = LudoShelf::Metadata::RomHashService::hashPlainFile(dataPath, dataPath);
        const QString audioPath = temporary.filePath("audio.bin");
        QFile audio(audioPath); QVERIFY(audio.open(QIODevice::WriteOnly)); audio.write("audio payload"); audio.close();
        const QString cuePath = temporary.filePath("disc.cue");
        QFile cue(cuePath); QVERIFY(cue.open(QIODevice::WriteOnly | QIODevice::Text));
        cue.write("FILE \"data.bin\" BINARY\n  TRACK 01 MODE1/2352\n    INDEX 01 00:00:00\n"
                  "FILE \"audio.bin\" BINARY\n  TRACK 02 AUDIO\n    INDEX 01 00:00:00\n");
        cue.close();

        const auto batch = LudoShelf::Metadata::RomHashService::discoverAndHash(cuePath, QFileInfo(cuePath).lastModified());
        QCOMPARE(batch.candidates.size(), 1);
        QCOMPARE(batch.candidates.first().sha1, expected.sha1);
    }

    void routesArchivedRvzThroughTitleLookup() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString archivePath = writeArchiveWithMember(temporary, "Example Game (USA).rvz");
        QVERIFY(!archivePath.isEmpty());
        QVERIFY(LudoShelf::Metadata::RomHashService::requiresTitleLookup(archivePath));
        const auto batch = LudoShelf::Metadata::RomHashService::discoverAndHash(archivePath, QFileInfo(archivePath).lastModified());
        QVERIFY(!batch.unsupportedReason.isEmpty());
        QVERIFY(batch.candidates.isEmpty());
    }

    void readsExactMatchFromLibretroRdb() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString directory = writeFixtureRdb(temporary); QVERIFY(!directory.isEmpty());
        LudoShelf::Metadata::HashCandidate candidate;
        candidate.byteSize = 9; candidate.crc32 = "CBF43926";
        const auto result = LudoShelf::Metadata::LibretroDatabaseProvider::lookupInDirectory(
            directory, {candidate}, {"Nintendo NES", "Library title", false});
        QCOMPARE(result.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QCOMPARE(result.metadata.canonicalTitle, QString("Example Game"));
        QCOMPARE(result.metadata.platform, QString("Nintendo NES"));
        QCOMPARE(result.metadata.releaseYear, 1991);
        QCOMPARE(result.metadata.developer, QString("Example Studio"));
        QCOMPARE(result.metadata.publisher, QString("Example Publisher"));
        QCOMPARE(result.metadata.genres, QStringList{"Platformer"});
        QCOMPARE(result.metadata.playerCount, QString("1-2"));
        QCOMPARE(result.metadata.regions, QStringList{"USA"});
        QCOMPARE(result.metadata.languages, QStringList{"English"});
        QCOMPARE(result.metadata.matchedHashAlgorithm, QString("crc32"));
        QCOMPARE(result.metadata.provider, QString("libretro-database-1.22.1"));

        const auto nintendoAlias = LudoShelf::Metadata::LibretroDatabaseProvider::lookupInDirectory(
            directory, {candidate}, {"nintendo", "Library title", false});
        QCOMPARE(nintendoAlias.kind, LudoShelf::Metadata::MetadataResultKind::Match);
    }

    void readsUniqueTitleAndRegionMatchFromLibretroRdb() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString directory = writeFixtureRdb(temporary); QVERIFY(!directory.isEmpty());
        const auto result = LudoShelf::Metadata::LibretroDatabaseProvider::lookupByTitleInDirectory(
            directory, {"Nintendo NES", "Example Game (USA) [!]", false, "nes", "USA"});
        QCOMPARE(result.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QCOMPARE(result.metadata.canonicalTitle, QString("Example Game"));
        QCOMPARE(result.metadata.identityConfidence, QString("title-region-unique"));
        QVERIFY(result.metadata.matchedHash.isEmpty());

        const auto wrongRegion = LudoShelf::Metadata::LibretroDatabaseProvider::lookupByTitleInDirectory(
            directory, {"Nintendo NES", "Example Game", false, "nes", "Japan"});
        QCOMPARE(wrongRegion.kind, LudoShelf::Metadata::MetadataResultKind::NoMatch);
    }

    void rejectsSizeMismatchedCrcAndFallsBackForCustomPlatformNames() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString directory = writeFixtureRdb(temporary); QVERIFY(!directory.isEmpty());
        LudoShelf::Metadata::HashCandidate candidate;
        candidate.byteSize = 10; candidate.crc32 = "CBF43926";
        const auto noMatch = LudoShelf::Metadata::LibretroDatabaseProvider::lookupInDirectory(
            directory, {candidate}, {"nes", "Library title", false});
        QCOMPARE(noMatch.kind, LudoShelf::Metadata::MetadataResultKind::NoMatch);
        const auto noMatchForCustomSystem = LudoShelf::Metadata::LibretroDatabaseProvider::lookupInDirectory(
            directory, {candidate}, {"Unknown System", "Library title", false});
        QCOMPARE(noMatchForCustomSystem.kind, LudoShelf::Metadata::MetadataResultKind::NoMatch);
        candidate.byteSize = 9;
        const auto fallbackMatch = LudoShelf::Metadata::LibretroDatabaseProvider::lookupInDirectory(
            directory, {candidate}, {"Custom NES Library", "Library title", false});
        QCOMPARE(fallbackMatch.kind, LudoShelf::Metadata::MetadataResultKind::Match);
    }

    void readsTheBundledLibretroDatabase() {
        LudoShelf::Metadata::HashCandidate candidate;
        candidate.byteSize = 262160;
        candidate.crc32 = "3577AB04";
        const auto result = LudoShelf::Metadata::LibretroDatabaseProvider::lookupInDirectory(
            QStringLiteral(LUDOSHELF_TEST_LIBRETRO_DATABASE_ROOT), {candidate},
            {"Nintendo NES", "Library title", false});
        QCOMPARE(result.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QCOMPARE(result.metadata.canonicalTitle, QString("'89 Dennou Kyuusei Uranai (Japan)"));
        QCOMPARE(result.metadata.matchedHashAlgorithm, QString("crc32"));
    }

    void findsUniqueDreamcastTitleInBundledDatabase() {
        const auto result = LudoShelf::Metadata::LibretroDatabaseProvider::lookupByTitleInDirectory(
            QStringLiteral(LUDOSHELF_TEST_LIBRETRO_DATABASE_ROOT),
            {"Sega Dreamcast", "Crazy Taxi", false, "dreamcast", "USA"});
        QCOMPARE(result.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QCOMPARE(result.metadata.canonicalTitle, QString("Crazy Taxi (USA)"));
        QCOMPARE(result.metadata.identityConfidence, QString("title-region-unique"));
    }

    void findsDiscSpecificDreamcastTitleInBundledDatabase() {
        const auto result = LudoShelf::Metadata::LibretroDatabaseProvider::lookupByTitleInDirectory(
            QStringLiteral(LUDOSHELF_TEST_LIBRETRO_DATABASE_ROOT),
            {"Sega Dreamcast", "Shenmue (USA) (Disc 2)", false, "dreamcast", "USA"});
        QCOMPARE(result.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QCOMPARE(result.metadata.canonicalTitle, QString("Shenmue (USA) (Disc 2)"));
        QCOMPARE(result.metadata.identityConfidence, QString("title-region-unique"));
    }

    void toleratesParallelGameCubeRdbEntriesAndRevisionNaming() {
        const QString root = QStringLiteral(LUDOSHELF_TEST_LIBRETRO_DATABASE_ROOT);
        const auto avalanche = LudoShelf::Metadata::LibretroDatabaseProvider::lookupByTitleInDirectory(
            root, {"Nintendo GameCube", "1080 Avalanche (USA)", false, "nintendogamecb", "USA"});
        QCOMPARE(avalanche.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QVERIFY(avalanche.metadata.canonicalTitle.contains("Avalanche"));
        const auto revision = LudoShelf::Metadata::LibretroDatabaseProvider::lookupByTitleInDirectory(
            root, {"Nintendo GameCube", "Mario Party 4 (USA) (v1.01)", false, "nintendogamecb", "USA"});
        QCOMPARE(revision.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QVERIFY(revision.metadata.canonicalTitle.contains("Mario Party 4"));
    }

    void handlesPossessiveAndUnknownRevisionGameCubeFilenameVariants() {
        const QString root = QStringLiteral(LUDOSHELF_TEST_LIBRETRO_DATABASE_ROOT);
        const auto possessive = LudoShelf::Metadata::LibretroDatabaseProvider::lookupByTitleInDirectory(
            root, {"Nintendo GameCube", "Disneys Magical Mirror Starring Mickey Mouse (USA)", false, "nintendogamecb", "USA"});
        QCOMPARE(possessive.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QVERIFY(possessive.metadata.canonicalTitle.contains("Disney"));
        const auto unknownRevision = LudoShelf::Metadata::LibretroDatabaseProvider::lookupByTitleInDirectory(
            root, {"Nintendo GameCube", "Metroid Prime (USA) (v1.02)", false, "nintendogamecb", "USA"});
        QCOMPARE(unknownRevision.kind, LudoShelf::Metadata::MetadataResultKind::Match);
        QVERIFY(unknownRevision.metadata.canonicalTitle.contains("Metroid Prime"));
    }

    void locatesTheDatabaseBesideTheExecutable() {
        const QString bundled = QDir(QCoreApplication::applicationDirPath())
            .filePath("libretro-database-1.22.1/rdb");
        const QString expected = QDir(bundled).exists() && !QDir(bundled).entryList({"*.rdb"}, QDir::Files).isEmpty()
            ? bundled
            : QDir(LudoShelf::App::AppPaths::dataRoot()).filePath("libretro-database-1.22.1/rdb");
        QCOMPARE(LudoShelf::Metadata::LibretroDatabaseProvider::databaseRoot(), expected);
    }
};

QTEST_MAIN(TestRomMetadata)
#include "test_rom_metadata.moc"
