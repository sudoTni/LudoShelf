#include "DatParser.h"

#include <QFile>
#include <QXmlStreamReader>
#include <QTextStream>
#include <QRegularExpression>
#include <QFileInfo>

namespace LudoShelf::Dat {

ParsedDatResult DatParser::parseDatFile(const QString& filePath) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {{}, {}, false, "Failed to open file: " + filePath};
    }

    QByteArray head = f.read(512);
    f.close();

    if (head.contains("<?xml") || head.contains("<datafile")) {
        return parseLogiqxXml(filePath);
    } else {
        return parseClrMamePro(filePath);
    }
}

ParsedDatResult DatParser::parseLogiqxXml(const QString& filePath) {
    ParsedDatResult result;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = "Cannot open XML DAT file.";
        return result;
    }

    QXmlStreamReader xml(&f);
    QString currentGameName;

    while (!xml.atEnd() && !xml.hasError()) {
        QXmlStreamReader::TokenType token = xml.readNext();

        if (token == QXmlStreamReader::StartElement) {
            QString name = xml.name().toString().toLower();
            if (name == "header") {
                while (!(xml.tokenType() == QXmlStreamReader::EndElement && xml.name().toString().toLower() == "header") && !xml.atEnd()) {
                    xml.readNext();
                    if (xml.tokenType() == QXmlStreamReader::StartElement) {
                        QString tag = xml.name().toString().toLower();
                        if (tag == "name") result.header.name = xml.readElementText();
                        else if (tag == "version") result.header.version = xml.readElementText();
                        else if (tag == "author") result.header.author = xml.readElementText();
                        else if (tag == "category") result.header.category = xml.readElementText();
                    }
                }
            } else if (name == "game" || name == "machine") {
                currentGameName = xml.attributes().value("name").toString();
            } else if (name == "rom") {
                Database::DatEntry entry;
                entry.gameName = currentGameName;
                entry.romName = xml.attributes().value("name").toString();
                entry.size = xml.attributes().value("size").toLongLong();
                entry.crc32 = xml.attributes().value("crc").toString().toLower();
                entry.md5 = xml.attributes().value("md5").toString().toLower();
                entry.sha1 = xml.attributes().value("sha1").toString().toLower();
                result.entries.append(entry);
            }
        }
    }

    if (xml.hasError()) {
        result.errorMessage = xml.errorString();
        result.success = false;
    } else {
        result.success = true;
    }

    if (result.header.name.isEmpty()) {
        result.header.name = QFileInfo(filePath).completeBaseName();
    }

    return result;
}

ParsedDatResult DatParser::parseClrMamePro(const QString& filePath) {
    ParsedDatResult result;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.errorMessage = "Cannot open ClrMamePro DAT file.";
        return result;
    }

    const QString document = QString::fromUtf8(f.readAll());
    const auto matchingClose = [&document](qsizetype opening) {
        int depth = 0;
        bool quoted = false;
        for (qsizetype index = opening; index < document.size(); ++index) {
            const QChar character = document.at(index);
            if (character == '"' && (index == 0 || document.at(index - 1) != '\\')) quoted = !quoted;
            if (quoted) continue;
            if (character == '(') ++depth;
            else if (character == ')' && --depth == 0) return index;
        }
        return qsizetype(-1);
    };
    const auto attributes = [](const QString& block) {
        QHash<QString, QString> values;
        static const QRegularExpression attribute(
            R"re(\b([A-Za-z_][A-Za-z0-9_]*)\s+(?:"((?:\\.|[^"])*)"|([^\s()]+)))re");
        auto matches = attribute.globalMatch(block);
        while (matches.hasNext()) {
            const auto match = matches.next();
            QString value = match.captured(2).isEmpty() ? match.captured(3) : match.captured(2);
            value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
            values.insert(match.captured(1).toLower(), value);
        }
        return values;
    };

    // Header fields are valid outside game blocks.  The first name is the DAT
    // title; game names are read only from their explicitly delimited blocks.
    const qsizetype firstGame = QRegularExpression(QStringLiteral(R"re(\b(?:game|machine)\s*\()re"),
        QRegularExpression::CaseInsensitiveOption).match(document).capturedStart();
    const auto header = attributes(document.left(firstGame < 0 ? document.size() : firstGame));
    result.header.name = header.value("name");
    result.header.version = header.value("version");
    result.header.author = header.value("author");
    result.header.category = header.value("category");

    static const QRegularExpression gameStart(QStringLiteral(R"re(\b(?:game|machine)\s*\()re"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression romStart(QStringLiteral(R"re(\brom\s*\()re"), QRegularExpression::CaseInsensitiveOption);
    auto games = gameStart.globalMatch(document);
    while (games.hasNext()) {
        const auto gameMatch = games.next();
        const qsizetype opening = document.indexOf('(', gameMatch.capturedStart());
        const qsizetype closing = matchingClose(opening);
        if (opening < 0 || closing < 0) { result.errorMessage = "Unterminated game block in ClrMamePro DAT."; return result; }
        const QString gameBlock = document.mid(opening + 1, closing - opening - 1);
        const qsizetype firstRom = gameBlock.indexOf(romStart);
        const auto gameAttributes = attributes(gameBlock.left(firstRom < 0 ? gameBlock.size() : firstRom));
        const QString gameName = gameAttributes.value("name");
        auto roms = romStart.globalMatch(gameBlock);
        while (roms.hasNext()) {
            const auto romMatch = roms.next();
            const qsizetype romOpening = gameBlock.indexOf('(', romMatch.capturedStart());
            int depth = 0;
            bool quoted = false;
            qsizetype romClosing = -1;
            for (qsizetype index = romOpening; index < gameBlock.size(); ++index) {
                const QChar character = gameBlock.at(index);
                if (character == '"' && (index == 0 || gameBlock.at(index - 1) != '\\')) quoted = !quoted;
                if (quoted) continue;
                if (character == '(') ++depth;
                else if (character == ')' && --depth == 0) { romClosing = index; break; }
            }
            if (romOpening < 0 || romClosing < 0) { result.errorMessage = "Unterminated ROM block in ClrMamePro DAT."; return result; }
            const auto rom = attributes(gameBlock.mid(romOpening + 1, romClosing - romOpening - 1));
            if (rom.value("name").isEmpty()) continue;
            Database::DatEntry entry;
            entry.gameName = gameName;
            entry.romName = rom.value("name");
            entry.size = rom.value("size").toLongLong();
            entry.crc32 = rom.value("crc").toLower();
            entry.md5 = rom.value("md5").toLower();
            entry.sha1 = rom.value("sha1").toLower();
            result.entries.append(entry);
        }
    }

    if (result.header.name.isEmpty()) {
        result.header.name = QFileInfo(filePath).completeBaseName();
    }

    result.success = true;
    return result;
}

} // namespace LudoShelf::Dat
