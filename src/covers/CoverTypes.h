#ifndef LUDOSHELF_COVERS_COVERTYPES_H
#define LUDOSHELF_COVERS_COVERTYPES_H

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QUrl>

namespace LudoShelf::Covers {

enum class CoverKind {
    BoxFront,
    BoxBack,
    BoxSpine,
    BoxFull,
    BoxThreeDimensional,
    Slipcover,
    JewelCaseFront,
    JewelCaseBack,
    CartridgeLabel,
    DiscLabel,
    CassetteCover,
    FloppyLabel,
    ArcadeFlyerFront,
    ArcadeFlyerBack,
    ArcadeCabinet,
    StoreVerticalCapsule,
    StoreHorizontalCapsule,
    LibraryVerticalArt,
    PromotionalPoster,
    FanCreatedCover,
    GeneratedPlaceholder,
    Unknown
};

enum class CoverScope {
    GameWork,
    PlatformRelease,
    RegionalRelease,
    Edition,
    Disc,
    StoreApplication,
    ArcadeMachine,
    UserGameRecord
};

struct CoverCandidate {
    QUuid id{QUuid::createUuid()};
    QString providerId;
    QString providerAssetId;
    QUrl sourcePage;
    QUrl downloadUrl;
    CoverKind kind{CoverKind::Unknown};
    CoverScope scope{CoverScope::GameWork};
    QString platformId;
    QString region;
    QStringList languages;
    QString edition;
    QString externalGameId;
    QString providerTitle;
    QString matchedLocalTitle;
    QString matchMethod;
    double matchConfidence{0.0};
    int declaredWidth{0};
    int declaredHeight{0};
    QString declaredMimeType;
    qint64 declaredBytes{0};
    QString rightsStatus{"unknown"};
    QString licenseId;
    QUrl licenseUrl;
    QString creator;
    QString attribution;
    double sourcePriority{0.0};
    double qualityScore{0.0};
    double finalScore{0.0};
};

struct MediaObject {
    QString sha256;
    QString relativePath;
    QString mimeType;
    QString extension;
    qint64 byteSize{0};
    int width{0};
    int height{0};
    QString perceptualHash;
    QString differenceHash;
    QString validationState;
    QDateTime createdAt;
    QDateTime validatedAt;
};

struct CoverAsset {
    QUuid id{QUuid::createUuid()};
    QUuid gameId;
    QString mediaObjectSha256;
    QString providerId;
    QString providerAssetId;
    QString providerGameId;
    CoverKind kind{CoverKind::Unknown};
    CoverScope scope{CoverScope::GameWork};
    QString platformId;
    QString region;
    QStringList languages;
    QString edition;
    QUrl sourcePage;
    QUrl sourceUrl;
    QString providerTitle;
    QString matchMethod;
    double matchConfidence{0.0};
    double qualityScore{0.0};
    double finalScore{0.0};
    QString rightsStatus{"unknown"};
    QString licenseId;
    QUrl licenseUrl;
    QString creator;
    QString attribution;
    bool redistributionAllowed{false};
    bool preferred{false};
    bool userSelected{false};
    bool userSupplied{false};
    bool locked{false};
    QDateTime downloadedAt;
    QDateTime providerUpdatedAt;
    QDateTime createdAt{QDateTime::currentDateTimeUtc()};
    QDateTime updatedAt{QDateTime::currentDateTimeUtc()};
};

struct CoverProvider {
    QString id;
    QString displayName;
    QString adapterVersion;
    QString credentialMode{"none"};
    QString stability;
    bool enabled{true};
    int priority{0};
    QString manifestJson;
    QDateTime lastSuccessAt;
    QDateTime lastFailureAt;
    QString circuitState{"closed"};
};

struct CoverJob {
    QUuid id{QUuid::createUuid()};
    QUuid gameId;
    QString providerId;
    QString operation;
    QString state{"created"};
    int priority{0};
    QString requestJson;
    QString resultJson;
    int attemptCount{0};
    QDateTime notBefore;
    int lastHttpStatus{0};
    QString lastErrorCode;
    QString lastErrorMessage;
    QDateTime createdAt{QDateTime::currentDateTimeUtc()};
    QDateTime updatedAt{QDateTime::currentDateTimeUtc()};
    QDateTime completedAt;
};

QString coverKindToString(CoverKind kind);
CoverKind coverKindFromString(const QString& value);
QString coverScopeToString(CoverScope scope);
CoverScope coverScopeFromString(const QString& value);
bool isPrimaryDisplayCover(CoverKind kind);

} // namespace LudoShelf::Covers

#endif // LUDOSHELF_COVERS_COVERTYPES_H
