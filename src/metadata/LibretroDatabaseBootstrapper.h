#ifndef LUDOSHELF_METADATA_LIBRETRODATABASEBOOTSTRAPPER_H
#define LUDOSHELF_METADATA_LIBRETRODATABASEBOOTSTRAPPER_H

#include <QObject>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace LudoShelf::Metadata {

// Downloads the pinned Libretro RDB bundle only when neither the bundled nor
// application-managed copy is usable. The bundle is verified and extracted to
// a staging directory before it replaces the managed copy.
class LibretroDatabaseBootstrapper final : public QObject {
    Q_OBJECT
public:
    explicit LibretroDatabaseBootstrapper(QObject* parent = nullptr);
    void ensureAvailable();

signals:
    void downloadStarted();
    void downloadFinished(bool available, const QString& message);

private:
    void fail(const QString& message);
    void beginExtraction(const QString& archivePath, const QString& dataRoot);

    QNetworkAccessManager* m_network{nullptr};
    QNetworkReply* m_reply{nullptr};
    std::unique_ptr<QTemporaryDir> m_temporaryDirectory;
    std::unique_ptr<QFile> m_downloadFile;
    std::unique_ptr<QCryptographicHash> m_downloadHash;
    bool m_active{false};
};

} // namespace LudoShelf::Metadata

#endif // LUDOSHELF_METADATA_LIBRETRODATABASEBOOTSTRAPPER_H
