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

    QTextStream in(&f);
    QString currentGameName;
    static QRegularExpression romRegex(R"raw(rom\s*\(\s*name\s+"([^"]+)"\s+size\s+(\d+)\s+crc\s+([0-9a-fA-F]+))raw", QRegularExpression::CaseInsensitiveOption);


    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.startsWith("name \"")) {
            if (result.header.name.isEmpty()) {
                result.header.name = line.section('"', 1, 1);
            }
        } else if (line.startsWith("game (") || line.startsWith("machine (")) {
            currentGameName.clear();
        } else if (line.startsWith("name ")) {
            currentGameName = line.section('"', 1, 1);
        } else {
            auto match = romRegex.match(line);
            if (match.hasMatch()) {
                Database::DatEntry entry;
                entry.gameName = currentGameName;
                entry.romName = match.captured(1);
                entry.size = match.captured(2).toLongLong();
                entry.crc32 = match.captured(3).toLower();
                result.entries.append(entry);
            }
        }
    }

    if (result.header.name.isEmpty()) {
        result.header.name = QFileInfo(filePath).completeBaseName();
    }

    result.success = true;
    return result;
}

} // namespace LudoShelf::Dat
