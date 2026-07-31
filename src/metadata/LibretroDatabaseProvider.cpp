#include "LibretroDatabaseProvider.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QtConcurrentRun>
#include <QtEndian>

#include <algorithm>
#include <memory>

namespace LudoShelf::Metadata {
namespace {

struct MessageValue {
    enum class Type { Invalid, Null, String, Binary, Integer, Boolean, Array, Map } type{Type::Invalid};
    QString string;
    QByteArray bytes;
    qint64 integer{0};
    bool boolean{false};
    QList<MessageValue> array;
    QHash<QString, MessageValue> map;
};

class MessagePackReader {
public:
    MessagePackReader(const uchar* data, qsizetype size) : m_data(data), m_size(size) {}

    bool read(MessageValue& value, int depth = 0) {
        if (depth > 32 || m_pos >= m_size) return false;
        const uchar marker = m_data[m_pos++];
        if (marker <= 0x7f) { value.type = MessageValue::Type::Integer; value.integer = marker; return true; }
        if (marker >= 0xe0) { value.type = MessageValue::Type::Integer; value.integer = static_cast<qint8>(marker); return true; }
        if (marker >= 0xa0 && marker <= 0xbf) return readString(value, marker & 0x1f);
        if (marker >= 0x90 && marker <= 0x9f) return readArray(value, marker & 0x0f, depth);
        if (marker >= 0x80 && marker <= 0x8f) return readMap(value, marker & 0x0f, depth);

        switch (marker) {
        case 0xc0: value.type = MessageValue::Type::Null; return true;
        case 0xc2: value.type = MessageValue::Type::Boolean; value.boolean = false; return true;
        case 0xc3: value.type = MessageValue::Type::Boolean; value.boolean = true; return true;
        case 0xc4: return readBinary(value, readUnsigned(1));
        case 0xc5: return readBinary(value, readUnsigned(2));
        case 0xc6: return readBinary(value, readUnsigned(4));
        case 0xcc: return readInteger(value, readUnsigned(1));
        case 0xcd: return readInteger(value, readUnsigned(2));
        case 0xce: return readInteger(value, readUnsigned(4));
        case 0xcf: return readInteger(value, readUnsigned(8));
        case 0xd0: return readInteger(value, readSigned(1));
        case 0xd1: return readInteger(value, readSigned(2));
        case 0xd2: return readInteger(value, readSigned(4));
        case 0xd3: return readInteger(value, readSigned(8));
        case 0xd9: return readString(value, readUnsigned(1));
        case 0xda: return readString(value, readUnsigned(2));
        case 0xdb: return readString(value, readUnsigned(4));
        case 0xdc: return readArray(value, readUnsigned(2), depth);
        case 0xdd: return readArray(value, readUnsigned(4), depth);
        case 0xde: return readMap(value, readUnsigned(2), depth);
        case 0xdf: return readMap(value, readUnsigned(4), depth);
        // Floating point and extension values are not used by the game
        // records, but consuming them keeps unknown future fields safe.
        case 0xca: return skip(4, value);
        case 0xcb: return skip(8, value);
        case 0xc7: return skipExtension(readUnsigned(1), value);
        case 0xc8: return skipExtension(readUnsigned(2), value);
        case 0xc9: return skipExtension(readUnsigned(4), value);
        case 0xd4: return skip(2, value);
        case 0xd5: return skip(3, value);
        case 0xd6: return skip(5, value);
        case 0xd7: return skip(9, value);
        case 0xd8: return skip(17, value);
        default: return false;
        }
    }

    qsizetype position() const { return m_pos; }

private:
    bool has(qsizetype length) const { return length >= 0 && m_pos <= m_size && length <= m_size - m_pos; }
    quint64 readUnsigned(int bytes) {
        if (!has(bytes)) { m_ok = false; return 0; }
        quint64 value = 0;
        for (int i = 0; i < bytes; ++i) value = (value << 8) | m_data[m_pos++];
        return value;
    }
    qint64 readSigned(int bytes) {
        const quint64 unsignedValue = readUnsigned(bytes);
        if (!m_ok) return 0;
        const int bits = bytes * 8;
        if (bits == 64) return static_cast<qint64>(unsignedValue);
        const quint64 signBit = quint64(1) << (bits - 1);
        return (unsignedValue & signBit) ? static_cast<qint64>(unsignedValue - (quint64(1) << bits)) : static_cast<qint64>(unsignedValue);
    }
    bool readInteger(MessageValue& value, qint64 integer) {
        if (!m_ok) return false;
        value.type = MessageValue::Type::Integer; value.integer = integer; return true;
    }
    bool readString(MessageValue& value, quint64 length) {
        if (!m_ok || length > static_cast<quint64>(m_size) || !has(static_cast<qsizetype>(length))) return false;
        value.type = MessageValue::Type::String;
        value.string = QString::fromUtf8(reinterpret_cast<const char*>(m_data + m_pos), static_cast<qsizetype>(length));
        m_pos += static_cast<qsizetype>(length); return true;
    }
    bool readBinary(MessageValue& value, quint64 length) {
        if (!m_ok || length > static_cast<quint64>(m_size) || !has(static_cast<qsizetype>(length))) return false;
        value.type = MessageValue::Type::Binary;
        value.bytes = QByteArray(reinterpret_cast<const char*>(m_data + m_pos), static_cast<qsizetype>(length));
        m_pos += static_cast<qsizetype>(length); return true;
    }
    bool readArray(MessageValue& value, quint64 length, int depth) {
        if (!m_ok || length > 4096) return false;
        value.type = MessageValue::Type::Array;
        value.array.reserve(static_cast<qsizetype>(length));
        for (quint64 i = 0; i < length; ++i) { MessageValue item; if (!read(item, depth + 1)) return false; value.array.append(std::move(item)); }
        return true;
    }
    bool readMap(MessageValue& value, quint64 length, int depth) {
        if (!m_ok || length > 4096) return false;
        value.type = MessageValue::Type::Map;
        for (quint64 i = 0; i < length; ++i) {
            MessageValue key, item;
            if (!read(key, depth + 1) || !read(item, depth + 1)) return false;
            if (key.type == MessageValue::Type::String) value.map.insert(key.string.toCaseFolded(), std::move(item));
        }
        return true;
    }
    bool skip(qsizetype length, MessageValue& value) {
        if (!has(length)) return false;
        m_pos += length; value.type = MessageValue::Type::Invalid; return true;
    }
    bool skipExtension(quint64 length, MessageValue& value) { return length > static_cast<quint64>(m_size - m_pos) ? false : skip(static_cast<qsizetype>(length + 1), value); }

    const uchar* m_data{nullptr};
    qsizetype m_size{0};
    qsizetype m_pos{0};
    bool m_ok{true};
};

QString normalizedName(QString value) {
    value = value.toCaseFolded();
    value.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")));
    return value;
}

QString databaseNameForPlatform(const QString& platform) {
    const QString key = normalizedName(platform);
    static const QHash<QString, QString> aliases{
        {"nes", "Nintendo - Nintendo Entertainment System"}, {"nintendo", "Nintendo - Nintendo Entertainment System"}, {"nintendones", "Nintendo - Nintendo Entertainment System"},
        {"snes", "Nintendo - Super Nintendo Entertainment System"}, {"supernes", "Nintendo - Super Nintendo Entertainment System"},
        {"supernintendo", "Nintendo - Super Nintendo Entertainment System"}, {"gb", "Nintendo - Game Boy"}, {"gbc", "Nintendo - Game Boy Color"},
        {"gba", "Nintendo - Game Boy Advance"}, {"n64", "Nintendo - Nintendo 64"}, {"nintendo64", "Nintendo - Nintendo 64"}, {"nds", "Nintendo - Nintendo DS"},
        {"gc", "Nintendo - GameCube"}, {"gcn", "Nintendo - GameCube"}, {"gamecube", "Nintendo - GameCube"},
        {"genesis", "Sega - Mega Drive - Genesis"}, {"megadrive", "Sega - Mega Drive - Genesis"}, {"md", "Sega - Mega Drive - Genesis"},
        {"segagenesis", "Sega - Mega Drive - Genesis"}, {"segamegadrive", "Sega - Mega Drive - Genesis"},
        {"dc", "Sega - Dreamcast"}, {"dreamcast", "Sega - Dreamcast"}, {"psx", "Sony - PlayStation"}, {"ps1", "Sony - PlayStation"},
        {"psp", "Sony - PlayStation Portable"}
    };
    return aliases.value(key, platform.trimmed());
}

struct DatabaseSelection {
    QStringList files;
    bool systemResolved{false};
};

DatabaseSelection databasesForContext(const QDir& directory, const RomLookupContext& context) {
    const QStringList fileNames = directory.entryList({"*.rdb"}, QDir::Files, QDir::Name);
    QStringList names{context.localPlatform, context.localPlatformShortName};
    names.append(databaseNameForPlatform(context.localPlatform));
    names.append(databaseNameForPlatform(context.localPlatformShortName));
    for (QString& name : names) name = normalizedName(name);
    names.removeAll(QString()); names.removeDuplicates();

    struct ScoredFile { QString path; int score{0}; qsizetype nameLength{0}; };
    QList<ScoredFile> scored;
    for (const QString& fileName : fileNames) {
        const QString baseName = QFileInfo(fileName).completeBaseName();
        const QString key = normalizedName(baseName);
        int score = 0;
        for (const QString& name : names) {
            if (name.isEmpty()) continue;
            if (key == name) score = qMax(score, 1000);
            else if (name.size() >= 4 && key.contains(name)) score = qMax(score, 100 + static_cast<int>(name.size()));
            else if (key.size() >= 4 && name.contains(key)) score = qMax(score, 50 + static_cast<int>(key.size()));
        }
        if (score > 0) scored.append({directory.filePath(fileName), score, key.size()});
    }
    std::sort(scored.begin(), scored.end(), [](const ScoredFile& left, const ScoredFile& right) {
        if (left.score != right.score) return left.score > right.score;
        if (left.nameLength != right.nameLength) return left.nameLength < right.nameLength;
        return left.path < right.path;
    });
    if (!scored.isEmpty()) {
        return {{scored.first().path}, true};
    }

    // LudoShelf permits custom system names. When a name cannot be mapped,
    // search every local RDB for an exact hash so no bundled Libretro system
    // becomes unreachable merely because of its display name.
    DatabaseSelection fallback;
    for (const QString& fileName : fileNames) fallback.files.append(directory.filePath(fileName));
    return fallback;
}

QString valueText(const MessageValue& value) {
    if (value.type == MessageValue::Type::String) return value.string.trimmed();
    if (value.type == MessageValue::Type::Binary) return QString::fromLatin1(value.bytes).trimmed();
    if (value.type == MessageValue::Type::Integer) return QString::number(value.integer);
    return {};
}

QString hashText(const MessageValue& value) {
    if (value.type != MessageValue::Type::Binary) return valueText(value);
    const bool isAsciiHex = !value.bytes.isEmpty() && std::all_of(value.bytes.cbegin(), value.bytes.cend(), [](char byte) {
        return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f') || (byte >= 'A' && byte <= 'F');
    });
    return isAsciiHex ? QString::fromLatin1(value.bytes) : QString::fromLatin1(value.bytes.toHex()).toUpper();
}

QStringList valueList(const MessageValue& value) {
    if (value.type != MessageValue::Type::Array) return valueText(value).isEmpty() ? QStringList{} : QStringList{valueText(value)};
    QStringList result;
    for (const auto& item : value.array) {
        const QString text = valueText(item);
        if (!text.isEmpty() && !result.contains(text, Qt::CaseInsensitive)) result.append(text);
    }
    return result;
}

QString valueFor(const QHash<QString, MessageValue>& fields, const QStringList& keys) {
    for (const auto& key : keys) {
        const QString value = valueText(fields.value(key));
        if (!value.isEmpty()) return value;
    }
    return {};
}

QStringList listFor(const QHash<QString, MessageValue>& fields, const QStringList& keys) {
    for (const auto& key : keys) {
        const QStringList values = valueList(fields.value(key));
        if (!values.isEmpty()) return values;
    }
    return {};
}

bool hashesMatch(const QHash<QString, MessageValue>& fields, const QList<HashCandidate>& candidates,
                 QString* algorithm, QString* matchedHash) {
    const QList<QPair<QString, int>> fieldsToCheck{{"sha1", 40}, {"md5", 32}, {"crc", 8}};
    for (const auto& field : fieldsToCheck) {
        const QString databaseHash = normalizeHash(hashText(fields.value(field.first)), field.second);
        if (databaseHash.isEmpty()) continue;
        for (const auto& candidate : candidates) {
            const QString localHash = field.first == "sha1" ? candidate.sha1 : field.first == "md5" ? candidate.md5 : candidate.crc32;
            if (databaseHash != normalizeHash(localHash, field.second)) continue;
            if (field.first == "crc") {
                const qint64 expectedSize = valueFor(fields, {"size"}).toLongLong();
                if (expectedSize > 0 && expectedSize != candidate.byteSize) continue;
            }
            *algorithm = field.first == "crc" ? QStringLiteral("crc32") : field.first;
            *matchedHash = databaseHash;
            return true;
        }
    }
    return false;
}

ProviderLookupResult noMatch() {
    ProviderLookupResult result; result.kind = MetadataResultKind::NoMatch; result.message = QStringLiteral("No exact Libretro database match."); return result;
}

ProviderLookupResult invalidDatabase(const QString& path) {
    ProviderLookupResult result; result.kind = MetadataResultKind::PermanentError;
    result.message = QStringLiteral("Libretro database file is invalid: %1").arg(QFileInfo(path).fileName()); return result;
}

QString titleLookupKey(QString value) {
    value = value.toCaseFolded();
    // Keep parenthesized region, revision, and disc tags for the primary
    // match: they distinguish multi-disc releases that share one game title.
    value.remove(QRegularExpression(QStringLiteral(R"(\s*\[[^\]]*\])")));
    // Treat a missing possessive apostrophe as a filename spelling variant:
    // "Disneys" and "Disney's" name the same release, unlike a word break.
    value.remove(QRegularExpression(QStringLiteral(R"(['’])")));
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(" "));
    return value.simplified();
}

QString looseTitleLookupKey(QString value) {
    value = value.toCaseFolded();
    value.remove(QRegularExpression(QStringLiteral(R"(\s*[\[(][^\])]*[\])])")));
    value.remove(QRegularExpression(QStringLiteral(R"(['’])")));
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(" "));
    return value.simplified();
}

bool isRegionTag(const QString& tag) {
    static const QSet<QString> knownRegions{
        QStringLiteral("usa"), QStringLiteral("canada"), QStringLiteral("europe"), QStringLiteral("japan"),
        QStringLiteral("korea"), QStringLiteral("australia"), QStringLiteral("world"), QStringLiteral("asia")
    };
    const QStringList tokens = tag.toCaseFolded().split(QRegularExpression(QStringLiteral(R"([,;/])")), Qt::SkipEmptyParts);
    return !tokens.isEmpty() && std::all_of(tokens.cbegin(), tokens.cend(), [](const QString& token) {
        return knownRegions.contains(token.trimmed());
    });
}

bool isRevisionTag(const QString& tag) {
    return QRegularExpression(QStringLiteral(R"(^(?:v\d+(?:\.\d+)*|rev(?:ision)?\s*[a-z0-9.]+)$)"),
                              QRegularExpression::CaseInsensitiveOption).match(tag.trimmed()).hasMatch();
}

QString scopedTitleLookupKey(QString value) {
    // Remove only a region marker.  This retains disc and revision tags, so
    // "(USA) (Disc 1)" can safely match "(USA, Canada) (Disc 1)".
    value.remove(QRegularExpression(QStringLiteral(R"(\s*\[[^\]]*\])")));
    const QRegularExpression parenthesized(QStringLiteral(R"(\s*\(([^)]*)\))"));
    QRegularExpressionMatchIterator matches = parenthesized.globalMatch(value);
    QList<QPair<int, int>> removals;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (isRegionTag(match.captured(1))) removals.prepend({match.capturedStart(0), match.capturedLength(0)});
    }
    for (const auto& removal : removals) value.remove(removal.first, removal.second);
    value = value.toCaseFolded();
    value.remove(QRegularExpression(QStringLiteral(R"(['’])")));
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(" "));
    return value.simplified();
}

QString revisionAgnosticTitleLookupKey(QString value) {
    // The RDB may carry only the base/rev-1 record for a dump labelled with a
    // later version.  Dropping only the revision preserves region and disc
    // identity while allowing its shared curated metadata to be used.
    value.remove(QRegularExpression(QStringLiteral(R"(\s*\[[^\]]*\])")));
    const QRegularExpression parenthesized(QStringLiteral(R"(\s*\(([^)]*)\))"));
    QRegularExpressionMatchIterator matches = parenthesized.globalMatch(value);
    QList<QPair<int, int>> removals;
    while (matches.hasNext()) {
        const QRegularExpressionMatch match = matches.next();
        if (isRevisionTag(match.captured(1))) removals.prepend({match.capturedStart(0), match.capturedLength(0)});
    }
    for (const auto& removal : removals) value.remove(removal.first, removal.second);
    value = value.toCaseFolded();
    value.remove(QRegularExpression(QStringLiteral(R"(['’])")));
    value.replace(QRegularExpression(QStringLiteral(R"([^\p{L}\p{N}]+)")), QStringLiteral(" "));
    return value.simplified();
}

QStringList titleLookupVariants(const QString& title) {
    QStringList values{title};
    const auto append = [&values](const QString& value) {
        if (!value.trimmed().isEmpty() && !values.contains(value, Qt::CaseInsensitive)) values.append(value);
    };
    QString alias = title;
    alias.replace(QStringLiteral("Chibi-Robo! Plug into Adventure!"), QStringLiteral("Chibi-Robo!"), Qt::CaseInsensitive);
    alias.replace(QStringLiteral("Cubivore"), QStringLiteral("Cubivore - Survival of the Fittest"), Qt::CaseInsensitive);
    alias.replace(QStringLiteral("Collectors Edition"), QStringLiteral("Collector's Edition"), Qt::CaseInsensitive);
    append(alias);
    const QStringList originals = values;
    const QRegularExpression versionTag(QStringLiteral(R"(\(v1\.(\d+)\))"), QRegularExpression::CaseInsensitiveOption);
    for (const QString& value : originals) {
        const QRegularExpressionMatch match = versionTag.match(value);
        if (!match.hasMatch()) continue;
        QString revised = value;
        revised.replace(match.capturedStart(0), match.capturedLength(0),
                        QStringLiteral("(Rev %1)").arg(match.captured(1).toInt()));
        append(revised);
    }
    return values;
}

bool hasRequestedRegion(const QHash<QString, MessageValue>& fields, const QString& requestedRegion) {
    if (requestedRegion.trimmed().isEmpty()) return true;
    const QStringList regions = listFor(fields, {"region", "regions", "country"});
    // Many Libretro USA records intentionally omit a region field.  They are
    // compatible with an explicit USA filename, while other regions remain
    // strict to avoid cross-region guesses.
    if (regions.isEmpty()) return requestedRegion.compare(QStringLiteral("USA"), Qt::CaseInsensitive) == 0;
    for (const QString& region : regions) {
        const QStringList tokens = region.split(QRegularExpression(QStringLiteral(R"([,;/])")), Qt::SkipEmptyParts);
        for (const QString& token : tokens)
            if (token.trimmed().compare(requestedRegion, Qt::CaseInsensitive) == 0) return true;
    }
    return false;
}

RomMetadata metadataFromRecord(const QString& path, const QHash<QString, MessageValue>& fields,
                               const RomLookupContext& context, const QString& identityConfidence,
                               const QString& recordId, const QString& algorithm = {}, const QString& matchedHash = {}) {
    RomMetadata metadata;
    metadata.canonicalTitle = valueFor(fields, {"name", "title"});
    if (metadata.canonicalTitle.isEmpty()) metadata.canonicalTitle = context.libraryTitle;
    const QString databasePlatform = QFileInfo(path).completeBaseName();
    metadata.platform = databasePlatform.contains("32X", Qt::CaseInsensitive)
        ? databasePlatform : (context.localPlatform.trimmed().isEmpty() ? databasePlatform : context.localPlatform.trimmed());
    metadata.releaseYear = valueFor(fields, {"releaseyear", "year", "releasedate"}).left(4).toInt();
    metadata.publisher = valueFor(fields, {"publisher", "company"});
    metadata.developer = valueFor(fields, {"developer", "author"});
    metadata.genres = listFor(fields, {"genre", "genres", "category"});
    metadata.playerCount = valueFor(fields, {"users", "players", "playercount"});
    metadata.description = valueFor(fields, {"description", "overview", "summary"});
    if (metadata.description.compare(metadata.canonicalTitle, Qt::CaseInsensitive) == 0) metadata.description.clear();
    metadata.regions = listFor(fields, {"region", "regions", "country"});
    if (metadata.regions.isEmpty() && identityConfidence.startsWith(QStringLiteral("title-region")) &&
        !context.libraryRegion.trimmed().isEmpty())
        metadata.regions = {context.libraryRegion.trimmed()};
    metadata.languages = listFor(fields, {"language", "languages"});
    metadata.revision = valueFor(fields, {"revision", "version"});
    metadata.mediaType = valueFor(fields, {"media", "mediatype", "romtype"});
    metadata.developmentStatus = valueFor(fields, {"status", "developmentstatus"});
    metadata.provider = QStringLiteral("libretro-database-1.22.1");
    metadata.providerRecordId = recordId;
    metadata.dumpSource = QStringLiteral("Libretro Database 1.22.1");
    metadata.identityConfidence = identityConfidence;
    metadata.metadataConfidence = QStringLiteral("curated");
    metadata.matchedHashAlgorithm = algorithm;
    metadata.matchedHash = matchedHash;
    metadata.fetchedAt = QDateTime::currentDateTimeUtc();
    for (const QString& field : {QStringLiteral("canonicalTitle"), QStringLiteral("platform"), QStringLiteral("releaseYear"),
                                QStringLiteral("publisher"), QStringLiteral("developer"), QStringLiteral("genres"),
                                QStringLiteral("playerCount"), QStringLiteral("description"), QStringLiteral("regions"),
                                QStringLiteral("languages"), QStringLiteral("revision"), QStringLiteral("mediaType"),
                                QStringLiteral("developmentStatus")})
        metadata.fieldProvenance.insert(field, {metadata.dumpSource, metadata.identityConfidence});
    return metadata;
}

ProviderLookupResult lookupFile(const QString& path, const QList<HashCandidate>& candidates, const RomLookupContext& context) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return invalidDatabase(path);
    const qint64 size = file.size();
    if (size < 17) return invalidDatabase(path);
    uchar* mapped = file.map(0, size);
    QByteArray owned;
    const uchar* data = mapped;
    if (!data) { owned = file.readAll(); data = reinterpret_cast<const uchar*>(owned.constData()); }
    const auto finish = [&] { if (mapped) file.unmap(mapped); };
    if (!data || size < 16 || QByteArray(reinterpret_cast<const char*>(data), 8) != QByteArrayLiteral("RARCHDB\0")) { finish(); return invalidDatabase(path); }
    const quint64 metadataOffset = qFromBigEndian<quint64>(data + 8);
    if (metadataOffset <= 16 || metadataOffset > static_cast<quint64>(size)) { finish(); return invalidDatabase(path); }

    MessagePackReader reader(data + 16, static_cast<qsizetype>(metadataOffset - 16));
    while (reader.position() < static_cast<qsizetype>(metadataOffset - 16)) {
        MessageValue record;
        if (!reader.read(record)) { finish(); return invalidDatabase(path); }
        if (record.type == MessageValue::Type::Null) break;
        if (record.type != MessageValue::Type::Map) { finish(); return invalidDatabase(path); }
        QString algorithm;
        QString matchedHash;
        if (!hashesMatch(record.map, candidates, &algorithm, &matchedHash)) continue;

        const RomMetadata metadata = metadataFromRecord(path, record.map, context, QStringLiteral("exact-%1").arg(algorithm),
                                                         QFileInfo(path).completeBaseName() + QLatin1Char(':') + matchedHash,
                                                         algorithm, matchedHash);
        ProviderLookupResult result;
        result.kind = MetadataResultKind::Match;
        result.metadata = metadata;
        result.rawResponse = QStringLiteral("%1 (%2 exact match)").arg(QFileInfo(path).fileName(), algorithm);
        finish();
        return result;
    }
    finish();
    return noMatch();
}

struct IndexedTitleRecord {
    QHash<QString, MessageValue> fields;
    QString exactKey;
    QString scopedKey;
    QString revisionAgnosticKey;
    QString looseKey;
    QString identity;
};

struct TitleIndex {
    QList<IndexedTitleRecord> records;
    QHash<QString, QList<int>> byExactKey;
    QHash<QString, QList<int>> byScopedKey;
    QHash<QString, QList<int>> byRevisionAgnosticKey;
    QHash<QString, QList<int>> byLooseKey;
};

QString titleRecordIdentity(const QHash<QString, MessageValue>& fields) {
    return QStringLiteral("%1|%2|%3|%4|%5")
        .arg(valueFor(fields, {"name", "title"}), hashText(fields.value("sha1")),
             hashText(fields.value("md5")), hashText(fields.value("crc")), valueFor(fields, {"size"}));
}

std::shared_ptr<const TitleIndex> titleIndexForFile(const QString& path, ProviderLookupResult* failure) {
    static QMutex mutex;
    static QHash<QString, std::shared_ptr<const TitleIndex>> indexes;
    const QString key = QFileInfo(path).canonicalFilePath();
    const QString cacheKey = key.isEmpty() ? QFileInfo(path).absoluteFilePath() : key;
    QMutexLocker lock(&mutex);
    if (const auto cached = indexes.constFind(cacheKey); cached != indexes.cend()) return cached.value();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *failure = invalidDatabase(path); return {}; }
    const qint64 size = file.size();
    if (size < 17) { *failure = invalidDatabase(path); return {}; }
    uchar* mapped = file.map(0, size);
    QByteArray owned;
    const uchar* data = mapped;
    if (!data) { owned = file.readAll(); data = reinterpret_cast<const uchar*>(owned.constData()); }
    const auto finish = [&] { if (mapped) file.unmap(mapped); };
    if (!data || QByteArray(reinterpret_cast<const char*>(data), 8) != QByteArrayLiteral("RARCHDB\0")) {
        finish(); *failure = invalidDatabase(path); return {};
    }
    const quint64 metadataOffset = qFromBigEndian<quint64>(data + 8);
    if (metadataOffset <= 16 || metadataOffset > static_cast<quint64>(size)) {
        finish(); *failure = invalidDatabase(path); return {};
    }

    auto index = std::make_shared<TitleIndex>();
    MessagePackReader reader(data + 16, static_cast<qsizetype>(metadataOffset - 16));
    while (reader.position() < static_cast<qsizetype>(metadataOffset - 16)) {
        MessageValue record;
        if (!reader.read(record) || (record.type != MessageValue::Type::Null && record.type != MessageValue::Type::Map)) {
            finish(); *failure = invalidDatabase(path); return {};
        }
        if (record.type == MessageValue::Type::Null) break;
        const QString title = valueFor(record.map, {"name", "title"});
        const QString exactKey = titleLookupKey(title);
        const QString scopedKey = scopedTitleLookupKey(title);
        const QString revisionAgnosticKey = revisionAgnosticTitleLookupKey(title);
        const QString looseKey = looseTitleLookupKey(title);
        if (exactKey.isEmpty() || scopedKey.isEmpty() || revisionAgnosticKey.isEmpty() || looseKey.isEmpty()) continue;
        const int recordIndex = static_cast<int>(index->records.size());
        index->records.append({record.map, exactKey, scopedKey, revisionAgnosticKey, looseKey, titleRecordIdentity(record.map)});
        index->byExactKey[exactKey].append(recordIndex);
        index->byScopedKey[scopedKey].append(recordIndex);
        index->byRevisionAgnosticKey[revisionAgnosticKey].append(recordIndex);
        index->byLooseKey[looseKey].append(recordIndex);
    }
    finish();
    indexes.insert(cacheKey, index);
    return index;
}

ProviderLookupResult lookupFileByTitle(const QString& path, const RomLookupContext& context) {
    ProviderLookupResult failure;
    const auto index = titleIndexForFile(path, &failure);
    if (!index) return failure;
    const auto selectRecord = [&index, &context](const QList<int>& candidates, bool fullLabel) -> QPair<int, bool> {
        QHash<QString, int> uniqueMatches;
        for (const int recordIndex : candidates) {
            const IndexedTitleRecord& record = index->records.at(recordIndex);
            if (hasRequestedRegion(record.fields, context.libraryRegion)) uniqueMatches.insert(record.identity, recordIndex);
        }
        if (uniqueMatches.size() == 1) return {uniqueMatches.constBegin().value(), true};
        // A full filename label includes disc/revision markers.  RDB files
        // often contain parallel DAT entries for that same label with
        // distinct hashes; use one curated record for metadata rather than
        // rejecting an otherwise exact title-and-region match.
        if (fullLabel && !uniqueMatches.isEmpty()) return {uniqueMatches.constBegin().value(), false};
        return {-1, false};
    };
    for (const QString& variant : titleLookupVariants(context.libraryTitle)) {
        const QString exactKey = titleLookupKey(variant);
        const QString scopedKey = scopedTitleLookupKey(variant);
        const QString revisionAgnosticKey = revisionAgnosticTitleLookupKey(variant);
        const QString looseKey = looseTitleLookupKey(variant);
        const QList<QPair<QList<int>, bool>> choices{
            {index->byExactKey.value(exactKey), true},
            {index->byScopedKey.value(scopedKey), false},
            {index->byRevisionAgnosticKey.value(revisionAgnosticKey), false},
            {index->byLooseKey.value(looseKey), false}
        };
        for (const auto& choice : choices) {
            if (choice.first.isEmpty()) continue;
            const auto selected = selectRecord(choice.first, choice.second);
            if (selected.first < 0) continue;
            const IndexedTitleRecord& record = index->records.at(selected.first);
            const QString recordId = QStringLiteral("%1:title-region:%2:%3")
                .arg(QFileInfo(path).completeBaseName(), record.exactKey, context.libraryRegion.toCaseFolded());
            ProviderLookupResult result;
            result.kind = MetadataResultKind::Match;
            result.metadata = metadataFromRecord(path, record.fields, context,
                selected.second ? QStringLiteral("title-region-unique") : QStringLiteral("title-region-label"), recordId);
            result.rawResponse = QStringLiteral("%1 (title and region match)").arg(QFileInfo(path).fileName());
            result.message = QStringLiteral("Metadata matched by title and region; exact disc identity was not verified.");
            return result;
        }
    }
    return noMatch();
}

} // namespace

LibretroDatabaseProvider::LibretroDatabaseProvider(QObject* parent) : RomMetadataProvider(parent) {}

QString LibretroDatabaseProvider::databaseRoot() {
    // Keep the portable distribution self-contained: the RDB bundle must be
    // next to the executable, independent of the current working directory.
    return QDir(QCoreApplication::applicationDirPath()).filePath("libretro-database-1.22.1/rdb");
}

bool LibretroDatabaseProvider::isAvailable() const {
    return QDir(databaseRoot()).exists() && !QDir(databaseRoot()).entryList({"*.rdb"}, QDir::Files).isEmpty();
}

QUuid LibretroDatabaseProvider::lookupByHashes(const QList<HashCandidate>& candidates, const RomLookupContext& context) {
    const QUuid requestId = QUuid::createUuid();
    auto* watcher = new QFutureWatcher<ProviderLookupResult>(this);
    connect(watcher, &QFutureWatcher<ProviderLookupResult>::finished, this, [this, watcher, requestId] {
        const ProviderLookupResult result = watcher->result();
        watcher->deleteLater();
        emit lookupFinished(requestId, result);
    });
    const QString root = databaseRoot();
    watcher->setFuture(QtConcurrent::run([root, candidates, context] { return lookupInDirectory(root, candidates, context); }));
    return requestId;
}

QUuid LibretroDatabaseProvider::lookupByTitle(const RomLookupContext& context) {
    const QUuid requestId = QUuid::createUuid();
    auto* watcher = new QFutureWatcher<ProviderLookupResult>(this);
    connect(watcher, &QFutureWatcher<ProviderLookupResult>::finished, this, [this, watcher, requestId] {
        const ProviderLookupResult result = watcher->result();
        watcher->deleteLater();
        emit lookupFinished(requestId, result);
    });
    const QString root = databaseRoot();
    watcher->setFuture(QtConcurrent::run([root, context] { return lookupByTitleInDirectory(root, context); }));
    return requestId;
}

ProviderLookupResult LibretroDatabaseProvider::lookupInDirectory(const QString& rdbDirectory,
                                                                  const QList<HashCandidate>& candidates,
                                                                  const RomLookupContext& context) {
    if (candidates.isEmpty()) { ProviderLookupResult result; result.kind = MetadataResultKind::PermanentError; result.message = QStringLiteral("No valid ROM hashes are available."); return result; }
    const QDir directory(rdbDirectory);
    if (!directory.exists()) { ProviderLookupResult result; result.kind = MetadataResultKind::PermanentError; result.message = QStringLiteral("Libretro database 1.22.1 is not installed."); return result; }
    const DatabaseSelection selection = databasesForContext(directory, context);
    if (selection.files.isEmpty()) {
        ProviderLookupResult result; result.kind = MetadataResultKind::PermanentError;
        result.message = QStringLiteral("Libretro database 1.22.1 contains no system databases."); return result;
    }
    QStringList databaseFiles = selection.files;
    if (context.libraryTitle.contains("32x", Qt::CaseInsensitive)) {
        const QString thirtyTwoX = directory.filePath("Sega - 32X.rdb");
        if (QFileInfo::exists(thirtyTwoX)) {
            databaseFiles.removeAll(thirtyTwoX);
            databaseFiles.prepend(thirtyTwoX);
        }
    }
    for (const QString& databaseFile : databaseFiles) {
        const ProviderLookupResult result = lookupFile(databaseFile, candidates, context);
        if (result.kind == MetadataResultKind::Match || result.kind == MetadataResultKind::PermanentError) return result;
    }
    return noMatch();
}

ProviderLookupResult LibretroDatabaseProvider::lookupByTitleInDirectory(const QString& rdbDirectory,
                                                                          const RomLookupContext& context) {
    const QDir directory(rdbDirectory);
    if (!directory.exists()) {
        ProviderLookupResult result;
        result.kind = MetadataResultKind::PermanentError;
        result.message = QStringLiteral("Libretro database 1.22.1 is not installed.");
        return result;
    }
    const DatabaseSelection selection = databasesForContext(directory, context);
    // Name matching across unrelated system databases is unsafe.  A custom
    // system name can still use exact hash lookup, but not this fallback.
    if (!selection.systemResolved || selection.files.isEmpty()) return noMatch();
    return lookupFileByTitle(selection.files.first(), context);
}

} // namespace LudoShelf::Metadata
