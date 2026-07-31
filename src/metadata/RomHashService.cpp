#include "RomHashService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QTemporaryDir>

#include <archive.h>
#include <archive_entry.h>
#include <zlib.h>

#include <algorithm>

namespace LudoShelf::Metadata {
namespace {

bool isCancelled(const std::shared_ptr<std::atomic_bool>& cancelled) {
    return cancelled && cancelled->load();
}

bool ignoredArchiveMember(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    return QStringList{"txt", "nfo", "diz", "jpg", "jpeg", "png", "gif", "webp", "pdf", "html", "url"}.contains(suffix);
}

HashCandidate hashDevice(const QString& payloadKey, qint64 size, const std::function<qint64(char*, qint64)>& read,
                         const std::shared_ptr<std::atomic_bool>& cancelled) {
    HashCandidate candidate;
    candidate.payloadKey = payloadKey;
    candidate.byteSize = size;
    if (size <= 0) return candidate;
    QCryptographicHash md5(QCryptographicHash::Md5), sha1(QCryptographicHash::Sha1), sha256(QCryptographicHash::Sha256);
    uLong crc = crc32(0L, Z_NULL, 0);
    QByteArray buffer(128 * 1024, Qt::Uninitialized);
    for (;;) {
        if (isCancelled(cancelled)) return {};
        const qint64 count = read(buffer.data(), buffer.size());
        if (count < 0) return {};
        if (count == 0) break;
        const QByteArrayView chunk(buffer.constData(), count);
        md5.addData(chunk); sha1.addData(chunk); sha256.addData(chunk);
        crc = crc32(crc, reinterpret_cast<const Bytef*>(buffer.constData()), static_cast<uInt>(count));
    }
    candidate.crc32 = QString("%1").arg(static_cast<quint32>(crc), 8, 16, QChar('0')).toUpper();
    candidate.md5 = QString::fromLatin1(md5.result().toHex()).toUpper();
    candidate.sha1 = QString::fromLatin1(sha1.result().toHex()).toUpper();
    candidate.sha256 = QString::fromLatin1(sha256.result().toHex()).toUpper();
    return candidate;
}

HashCandidate hashSha1Device(const QString& payloadKey, qint64 size, const std::function<qint64(char*, qint64)>& read,
                             const std::shared_ptr<std::atomic_bool>& cancelled) {
    HashCandidate candidate;
    candidate.payloadKey = payloadKey;
    candidate.byteSize = size;
    if (size <= 0) return candidate;
    QCryptographicHash sha1(QCryptographicHash::Sha1);
    QByteArray buffer(128 * 1024, Qt::Uninitialized);
    for (;;) {
        if (isCancelled(cancelled)) return {};
        const qint64 count = read(buffer.data(), buffer.size());
        if (count < 0) return {};
        if (count == 0) break;
        sha1.addData(QByteArrayView(buffer.constData(), count));
    }
    candidate.sha1 = QString::fromLatin1(sha1.result().toHex()).toUpper();
    return candidate;
}

HashCandidate hashSmdDevice(const QString& payloadKey, qint64 encodedSize,
                            const std::function<qint64(char*, qint64)>& read,
                            const std::shared_ptr<std::atomic_bool>& cancelled) {
    // GoodGen SMD dumps have a 512-byte copier header followed by 16 KiB
    // blocks containing odd bytes then even bytes. Libretro DAT/RDB hashes
    // the canonical linear ROM image, so hash that reconstructed stream.
    constexpr qint64 HeaderSize = 512;
    constexpr qint64 BlockSize = 16 * 1024;
    if (encodedSize <= HeaderSize || (encodedSize - HeaderSize) % BlockSize != 0) return {};
    const auto readExactly = [&read](char* data, qint64 length) {
        qint64 offset = 0;
        while (offset < length) {
            const qint64 count = read(data + offset, length - offset);
            if (count <= 0) return false;
            offset += count;
        }
        return true;
    };
    QByteArray header(HeaderSize, Qt::Uninitialized);
    if (!readExactly(header.data(), header.size())) return {};

    HashCandidate candidate;
    candidate.payloadKey = payloadKey;
    candidate.byteSize = encodedSize - HeaderSize;
    QCryptographicHash md5(QCryptographicHash::Md5), sha1(QCryptographicHash::Sha1), sha256(QCryptographicHash::Sha256);
    uLong crc = crc32(0L, Z_NULL, 0);
    QByteArray encoded(BlockSize, Qt::Uninitialized);
    QByteArray decoded(BlockSize, Qt::Uninitialized);
    for (qint64 remaining = candidate.byteSize; remaining > 0; remaining -= BlockSize) {
        if (isCancelled(cancelled) || !readExactly(encoded.data(), encoded.size())) return {};
        for (int index = 0; index < BlockSize / 2; ++index) {
            decoded[2 * index] = encoded[BlockSize / 2 + index];
            decoded[2 * index + 1] = encoded[index];
        }
        md5.addData(decoded); sha1.addData(decoded); sha256.addData(decoded);
        crc = crc32(crc, reinterpret_cast<const Bytef*>(decoded.constData()), static_cast<uInt>(decoded.size()));
    }
    candidate.crc32 = QString("%1").arg(static_cast<quint32>(crc), 8, 16, QChar('0')).toUpper();
    candidate.md5 = QString::fromLatin1(md5.result().toHex()).toUpper();
    candidate.sha1 = QString::fromLatin1(sha1.result().toHex()).toUpper();
    candidate.sha256 = QString::fromLatin1(sha256.result().toHex()).toUpper();
    return candidate;
}

HashCandidate hashINesDevice(const QString& payloadKey, qint64 encodedSize,
                             const std::function<qint64(char*, qint64)>& read,
                             const std::shared_ptr<std::atomic_bool>& cancelled) {
    // iNES/NES 2.0 files prepend a 16-byte container header, and may contain
    // a 512-byte trainer immediately after it.  Libretro's RDB signatures
    // identify the cartridge payload, not either container component.
    constexpr qint64 HeaderSize = 16;
    constexpr qint64 TrainerSize = 512;
    if (encodedSize <= HeaderSize) return {};
    const auto readExactly = [&read](char* data, qint64 length) {
        qint64 offset = 0;
        while (offset < length) {
            const qint64 count = read(data + offset, length - offset);
            if (count <= 0) return false;
            offset += count;
        }
        return true;
    };
    QByteArray header(HeaderSize, Qt::Uninitialized);
    if (!readExactly(header.data(), header.size()) || header.left(4) != QByteArrayLiteral("NES\x1A")) return {};
    const qint64 trainerSize = (static_cast<unsigned char>(header.at(6)) & 0x04) ? TrainerSize : 0;
    if (encodedSize <= HeaderSize + trainerSize) return {};
    if (trainerSize > 0) {
        QByteArray trainer(TrainerSize, Qt::Uninitialized);
        if (!readExactly(trainer.data(), trainer.size())) return {};
    }
    return hashDevice(payloadKey, encodedSize - HeaderSize - trainerSize, read, cancelled);
}

QList<QString> cueDataTracks(const QString& cuePath) {
    QFile cue(cuePath);
    if (!cue.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QStringList tracks;
    const QRegularExpression fileLine(R"re(^\s*FILE\s+"([^"]+)"\s+\S+\s*$)re", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression trackLine(R"re(^\s*TRACK\s+\d+\s+(\S+)\s*$)re", QRegularExpression::CaseInsensitiveOption);
    QString currentFile;
    QTextStream stream(&cue);
    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        const auto fileMatch = fileLine.match(line);
        if (fileMatch.hasMatch()) {
            currentFile = QFileInfo(QFileInfo(cuePath).dir(), fileMatch.captured(1)).absoluteFilePath();
            continue;
        }
        const auto trackMatch = trackLine.match(line);
        if (trackMatch.hasMatch() && !currentFile.isEmpty() &&
            !trackMatch.captured(1).startsWith(QStringLiteral("AUDIO"), Qt::CaseInsensitive)) {
            tracks.append(currentFile);
        }
    }
    tracks.removeDuplicates();
    return tracks;
}

RomHashBatch hashChdCdTracks(const QString& path, const std::shared_ptr<std::atomic_bool>& cancelled) {
    RomHashBatch batch;
    const QString chdman = QStandardPaths::findExecutable(QStringLiteral("chdman"));
    if (chdman.isEmpty()) {
        batch.unsupportedReason = "Exact CHD metadata requires chdman. Install MAME tools (the chdman executable) and refresh ROM metadata.";
        return batch;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        batch.error = "A temporary directory could not be created for CHD track extraction.";
        return batch;
    }
    const QString cuePath = temporary.filePath(QStringLiteral("disc.cue"));
    QProcess process;
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(chdman, {QStringLiteral("extractcd"), QStringLiteral("-i"), path, QStringLiteral("-o"), cuePath});
    if (!process.waitForStarted(5000)) {
        batch.error = "chdman could not be started to extract CD tracks.";
        return batch;
    }
    constexpr int PollIntervalMs = 250;
    constexpr int ExtractionTimeoutMs = 10 * 60 * 1000;
    int elapsedMs = 0;
    while (!process.waitForFinished(PollIntervalMs)) {
        if (isCancelled(cancelled)) {
            process.kill();
            process.waitForFinished();
            return {};
        }
        elapsedMs += PollIntervalMs;
        if (elapsedMs >= ExtractionTimeoutMs) {
            process.kill();
            process.waitForFinished();
            batch.error = "chdman timed out while extracting CD tracks.";
            return batch;
        }
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        batch.error = QString("chdman could not extract CD tracks (exit code %1).").arg(process.exitCode());
        return batch;
    }
    const QStringList tracks = cueDataTracks(cuePath);
    for (const QString& track : tracks) {
        if (batch.candidates.size() >= 50 || isCancelled(cancelled)) break;
        QFile extractedTrack(track);
        if (!extractedTrack.open(QIODevice::ReadOnly) || extractedTrack.size() <= 0) continue;
        const auto candidate = hashSha1Device(
            QStringLiteral("%1::chd-data-track::%2").arg(path, QFileInfo(track).fileName()), extractedTrack.size(),
            [&extractedTrack](char* data, qint64 length) { return extractedTrack.read(data, length); }, cancelled);
        if (!candidate.sha1.isEmpty()) batch.candidates.append(candidate);
    }
    if (batch.candidates.isEmpty()) batch.error = "chdman extracted no readable CD data tracks.";
    return batch;
}

struct ArchiveMember { QString path; qint64 size{0}; quint32 crc{0}; };

QList<ArchiveMember> archiveMembers(const QString& archivePath) {
    QList<ArchiveMember> results;
    archive *reader = archive_read_new();
    archive_read_support_filter_all(reader); archive_read_support_format_all(reader);
    if (archive_read_open_filename(reader, archivePath.toUtf8().constData(), 128 * 1024) != ARCHIVE_OK) { archive_read_free(reader); return {}; }
    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        const QString path = QString::fromUtf8(archive_entry_pathname(entry));
        const qint64 size = archive_entry_size(entry);
        if (!path.endsWith('/') && size > 0 && !ignoredArchiveMember(path)) results.append(ArchiveMember{path, size, 0});
        archive_read_data_skip(reader);
    }
    archive_read_free(reader);
    std::sort(results.begin(), results.end(), [](const auto& left, const auto& right) { return left.size > right.size; });
    return results;
}

HashCandidate hashArchiveMember(const QString& archivePath, const ArchiveMember& wanted,
                                 const std::shared_ptr<std::atomic_bool>& cancelled) {
    archive *reader = archive_read_new();
    archive_read_support_filter_all(reader); archive_read_support_format_all(reader);
    if (archive_read_open_filename(reader, archivePath.toUtf8().constData(), 128 * 1024) != ARCHIVE_OK) { archive_read_free(reader); return {}; }
    HashCandidate candidate;
    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        if (QString::fromUtf8(archive_entry_pathname(entry)) == wanted.path) {
            const QString key = QString("%1::%2::%3::%4").arg(archivePath, wanted.path).arg(wanted.size).arg(wanted.crc, 8, 16, QChar('0'));
            candidate = hashDevice(key, wanted.size, [reader](char* data, qint64 length) { return archive_read_data(reader, data, static_cast<size_t>(length)); }, cancelled);
            break;
        }
        archive_read_data_skip(reader);
    }
    archive_read_free(reader);
    return candidate;
}

HashCandidate hashSmdArchiveMember(const QString& archivePath, const ArchiveMember& wanted,
                                   const std::shared_ptr<std::atomic_bool>& cancelled) {
    archive *reader = archive_read_new();
    archive_read_support_filter_all(reader); archive_read_support_format_all(reader);
    if (archive_read_open_filename(reader, archivePath.toUtf8().constData(), 128 * 1024) != ARCHIVE_OK) { archive_read_free(reader); return {}; }
    HashCandidate candidate;
    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        if (QString::fromUtf8(archive_entry_pathname(entry)) == wanted.path) {
            const QString key = QString("%1::%2::smd-deinterleaved").arg(archivePath, wanted.path);
            candidate = hashSmdDevice(key, wanted.size,
                [reader](char* data, qint64 length) { return archive_read_data(reader, data, static_cast<size_t>(length)); }, cancelled);
            break;
        }
        archive_read_data_skip(reader);
    }
    archive_read_free(reader);
    return candidate;
}

HashCandidate hashINesArchiveMember(const QString& archivePath, const ArchiveMember& wanted,
                                    const std::shared_ptr<std::atomic_bool>& cancelled) {
    archive *reader = archive_read_new();
    archive_read_support_filter_all(reader); archive_read_support_format_all(reader);
    if (archive_read_open_filename(reader, archivePath.toUtf8().constData(), 128 * 1024) != ARCHIVE_OK) { archive_read_free(reader); return {}; }
    HashCandidate candidate;
    archive_entry *entry = nullptr;
    while (archive_read_next_header(reader, &entry) == ARCHIVE_OK) {
        if (QString::fromUtf8(archive_entry_pathname(entry)) == wanted.path) {
            const QString key = QString("%1::%2::ines-payload").arg(archivePath, wanted.path);
            candidate = hashINesDevice(key, wanted.size,
                [reader](char* data, qint64 length) { return archive_read_data(reader, data, static_cast<size_t>(length)); }, cancelled);
            break;
        }
        archive_read_data_skip(reader);
    }
    archive_read_free(reader);
    return candidate;
}

} // namespace

HashCandidate RomHashService::hashPlainFile(const QString& path, const QString& payloadKey,
                                            const std::shared_ptr<std::atomic_bool>& cancelled) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0) return {};
    return hashDevice(payloadKey, file.size(), [&file](char* data, qint64 length) { return file.read(data, length); }, cancelled);
}

RomHashBatch RomHashService::discoverAndHash(const QString& path, const QDateTime& modifiedAt,
                                             const std::shared_ptr<std::atomic_bool>& cancelled) {
    Q_UNUSED(modifiedAt);
    RomHashBatch batch;
    const QFileInfo file(path);
    if (!file.exists() || !file.isFile()) { batch.error = "ROM file is unavailable."; return batch; }
    const QString suffix = file.suffix().toLower();
    if (suffix == "chd") return hashChdCdTracks(path, cancelled);
    if (QStringList{"cso", "rvz"}.contains(suffix)) { batch.unsupportedReason = "This transformed disc container is not supported for exact remote identification."; return batch; }
    if (suffix == "cue") {
        for (const QString& track : cueDataTracks(path)) {
            if (batch.candidates.size() >= 50 || isCancelled(cancelled)) break;
            const auto candidate = hashPlainFile(track, track, cancelled);
            if (!candidate.sha256.isEmpty()) batch.candidates.append(candidate);
        }
        if (batch.candidates.isEmpty() && batch.unsupportedReason.isEmpty()) batch.error = "No readable data tracks were found in the CUE sheet.";
        return batch;
    }
    if (QStringList{"zip", "7z"}.contains(suffix)) {
        const auto members = archiveMembers(path);
        if (members.isEmpty()) { batch.error = "The archive has no supported content members or could not be read."; return batch; }
        if (std::any_of(members.cbegin(), members.cend(), [](const ArchiveMember& member) {
                return QStringList{"cso", "rvz"}.contains(QFileInfo(member.path).suffix().toLower());
            })) {
            batch.unsupportedReason = "This archive contains a transformed disc image that cannot be identified by content hash.";
            return batch;
        }
        for (const auto& member : members) {
            if (batch.candidates.size() >= 50 || isCancelled(cancelled)) break;
            const auto candidate = hashArchiveMember(path, member, cancelled);
            if (!candidate.sha256.isEmpty()) batch.candidates.append(candidate);
            if (QFileInfo(member.path).suffix().compare("smd", Qt::CaseInsensitive) == 0 && batch.candidates.size() < 50) {
                const auto canonical = hashSmdArchiveMember(path, member, cancelled);
                if (!canonical.sha256.isEmpty()) batch.candidates.append(canonical);
            }
            if (QFileInfo(member.path).suffix().compare("nes", Qt::CaseInsensitive) == 0 && batch.candidates.size() < 50) {
                const auto canonical = hashINesArchiveMember(path, member, cancelled);
                if (!canonical.sha256.isEmpty()) batch.candidates.append(canonical);
            }
        }
        return batch;
    }
    const auto candidate = hashPlainFile(path, path, cancelled);
    if (candidate.sha256.isEmpty()) batch.error = file.size() == 0 ? "Zero-byte ROM files cannot be identified." : "The ROM could not be hashed.";
    else {
        batch.candidates.append(candidate);
        if (suffix == "smd") {
            QFile smd(path);
            if (smd.open(QIODevice::ReadOnly)) {
                const auto canonical = hashSmdDevice(path + "::smd-deinterleaved", smd.size(),
                    [&smd](char* data, qint64 length) { return smd.read(data, length); }, cancelled);
                if (!canonical.sha256.isEmpty()) batch.candidates.append(canonical);
            }
        }
        if (suffix == "nes") {
            QFile ines(path);
            if (ines.open(QIODevice::ReadOnly)) {
                const auto canonical = hashINesDevice(path + "::ines-payload", ines.size(),
                    [&ines](char* data, qint64 length) { return ines.read(data, length); }, cancelled);
                if (!canonical.sha256.isEmpty()) batch.candidates.append(canonical);
            }
        }
    }
    return batch;
}

bool RomHashService::requiresTitleLookup(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (QStringList{"cso", "rvz"}.contains(suffix)) return true;
    if (!QStringList{"zip", "7z"}.contains(suffix)) return false;
    const auto members = archiveMembers(path);
    return std::any_of(members.cbegin(), members.cend(), [](const ArchiveMember& member) {
        return QStringList{"cso", "rvz"}.contains(QFileInfo(member.path).suffix().toLower());
    });
}

} // namespace LudoShelf::Metadata
