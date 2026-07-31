#include "LibretroCoverProvider.h"
#include "CoverTitleNormalizer.h"
#include "CoverScorer.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace LudoShelf::Covers {
namespace {
QString libretroSystemName(const Domain::System& system) {
    QString shortName = system.shortName.trimmed();
    if (shortName.isEmpty()) shortName = system.name;
    shortName = shortName.toCaseFolded();
    shortName.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")));
    static const QHash<QString, QString> names{
        {"nes", "Nintendo - Nintendo Entertainment System"},
        {"nintendo", "Nintendo - Nintendo Entertainment System"},
        {"snes", "Nintendo - Super Nintendo Entertainment System"},
        {"supernintendo", "Nintendo - Super Nintendo Entertainment System"},
        {"gb", "Nintendo - Game Boy"},
        {"gbc", "Nintendo - Game Boy Color"},
        {"gba", "Nintendo - Game Boy Advance"},
        {"n64", "Nintendo - Nintendo 64"},
        {"nintendo64", "Nintendo - Nintendo 64"},
        {"nds", "Nintendo - Nintendo DS"},
        {"gc", "Nintendo - GameCube"},
        {"gcn", "Nintendo - GameCube"},
        {"gamecube", "Nintendo - GameCube"},
        {"nintendogamecube", "Nintendo - GameCube"},
        {"nintendogamecb", "Nintendo - GameCube"},
        {"genesis", "Sega - Mega Drive - Genesis"},
        {"megadrive", "Sega - Mega Drive - Genesis"},
        {"md", "Sega - Mega Drive - Genesis"},
        {"segagenesis", "Sega - Mega Drive - Genesis"},
        {"segamegadrive", "Sega - Mega Drive - Genesis"},
        {"dc", "Sega - Dreamcast"},
        {"dreamcast", "Sega - Dreamcast"},
        {"psx", "Sony - PlayStation"},
        {"ps1", "Sony - PlayStation"},
        {"psp", "Sony - PlayStation Portable"}
    };
    return names.value(shortName, system.name);
}

QString collectionForPlatformName(const QString& platform) {
    const QString normalized = platform.trimmed().toCaseFolded();
    if (normalized == "sega - 32x" || normalized == "sega 32x" || normalized == "32x") return "Sega - 32X";
    if (normalized == "sega genesis" || normalized == "sega mega drive" || normalized == "sega - mega drive - genesis")
        return "Sega - Mega Drive - Genesis";
    if (normalized == "nintendo" || normalized == "nintendo entertainment system" || normalized == "nintendo - nintendo entertainment system")
        return "Nintendo - Nintendo Entertainment System";
    if (normalized == "super nintendo" || normalized == "super nintendo entertainment system" ||
        normalized == "nintendo - super nintendo entertainment system")
        return "Nintendo - Super Nintendo Entertainment System";
    if (normalized == "nintendo game boy" || normalized == "nintendo - game boy") return "Nintendo - Game Boy";
    if (normalized == "nintendo game boy color" || normalized == "nintendo - game boy color") return "Nintendo - Game Boy Color";
    if (normalized == "nintendo game boy advance" || normalized == "nintendo - game boy advance") return "Nintendo - Game Boy Advance";
    if (normalized == "nintendo 64" || normalized == "nintendo64" || normalized == "nintendo - nintendo 64") return "Nintendo - Nintendo 64";
    if (normalized == "nintendo ds" || normalized == "nintendo - nintendo ds") return "Nintendo - Nintendo DS";
    if (normalized == "nintendo gamecube" || normalized == "nintendo - gamecube") return "Nintendo - GameCube";
    if (normalized == "sega dreamcast" || normalized == "sega - dreamcast") return "Sega - Dreamcast";
    if (normalized == "sony playstation" || normalized == "sony - playstation") return "Sony - PlayStation";
    if (normalized == "sony playstation portable" || normalized == "sony - playstation portable") return "Sony - PlayStation Portable";
    return {};
}

QString assetFileName(const QString& assetPath) {
    return assetPath.section('/', -1);
}

QList<CoverCandidate> makeCatalogCandidates(const Domain::Game& game, const Domain::System& system,
                                            const QString& collection, const QStringList& assetPaths) {
    QList<CoverCandidate> candidates;
    QSet<QString> seen;
    const QString repository = QString(collection).replace(' ', '_');
    for (const QString& assetPath : assetPaths) {
        if (!assetPath.startsWith("Named_Boxarts/") || !assetPath.endsWith(".png", Qt::CaseInsensitive)) continue;
        const QString filename = assetFileName(assetPath);
        const QString key = collection + QLatin1Char('/') + filename;
        if (seen.contains(key)) continue;
        seen.insert(key);
        CoverCandidate candidate;
        candidate.providerId = QString::fromLatin1(LibretroCoverProvider::Id);
        candidate.providerAssetId = key;
        candidate.kind = CoverKind::BoxFront;
        candidate.scope = CoverScope::PlatformRelease;
        candidate.platformId = system.shortName;
        candidate.region = game.region;
        candidate.providerTitle = filename.left(filename.size() - 4);
        candidate.matchedLocalTitle = game.title;
        candidate.matchMethod = QStringLiteral("thumbnail_catalog_match");
        candidate.matchConfidence = 1.0;
        candidate.sourcePriority = 850;
        candidate.rightsStatus = QStringLiteral("personal_frontend_use_provider_collection");
        candidate.downloadUrl = QUrl(QStringLiteral("https://raw.githubusercontent.com/libretro-thumbnails/%1/master/Named_Boxarts/%2")
            .arg(repository, LibretroCoverProvider::encodePathSegment(filename)));
        candidate.sourcePage = candidate.downloadUrl;
        CoverScorer::score(candidate, game.region, false, true);
        candidates.append(candidate);
    }
    return candidates;
}
}

QString LibretroCoverProvider::encodePathSegment(const QString& value) {
    return QString::fromLatin1(QUrl::toPercentEncoding(value, QByteArray(), QByteArray()));
}

QString LibretroCoverProvider::collectionForSystem(const Domain::System& system) {
    const QString namedCollection = collectionForPlatformName(system.name);
    return namedCollection.isEmpty() ? libretroSystemName(system) : namedCollection;
}

QString LibretroCoverProvider::collectionForMetadataPlatform(const QString& platform) {
    return collectionForPlatformName(platform);
}

QList<CoverCandidate> LibretroCoverProvider::candidatesFor(const Domain::Game& game, const Domain::System& system,
                                                            const QStringList& titleCandidates,
                                                            const QString& collectionName) {
    QList<CoverCandidate> candidates;
    if (game.title.isEmpty() || system.name.isEmpty()) return candidates;
    const QString providerSystemName = collectionName.isEmpty() ? collectionForSystem(system) : collectionName;
    QStringList sourceNames = titleCandidates;
    if (sourceNames.isEmpty()) {
        const auto lookup = CoverTitleNormalizer::normalize(game.title);
        sourceNames = {game.title, lookup.shortTitle};
    }
    const QStringList names = CoverTitleNormalizer::libretroTitleCandidates(sourceNames, game.region);
    QStringList providerSystemNames{providerSystemName};
    const QString titleText = sourceNames.join(' ').toCaseFolded();
    if (titleText.contains("32x") && !providerSystemNames.contains("Sega - 32X")) providerSystemNames.append("Sega - 32X");
    QSet<QString> seen;
    int candidateIndex = 0;
    for (const QString& providerSystem : providerSystemNames) for (const QString& originalName : names) {
        const int index = candidateIndex++;
        const QString name = CoverTitleNormalizer::libretroSanitize(originalName);
        const QString key = providerSystem + QLatin1Char('/') + name;
        if (name.isEmpty() || seen.contains(key)) continue;
        seen.insert(key);
        CoverCandidate candidate;
        candidate.providerId = QString::fromLatin1(Id);
        candidate.kind = CoverKind::BoxFront;
        candidate.scope = CoverScope::PlatformRelease;
        candidate.platformId = system.shortName;
        candidate.region = game.region;
        candidate.providerTitle = originalName;
        candidate.matchedLocalTitle = game.title;
        candidate.matchMethod = index == 0 ? QStringLiteral("exact_rom_filename") : QStringLiteral("normalized_title_variant");
        candidate.matchConfidence = index == 0 ? 0.98 : 0.85;
        candidate.sourcePriority = 825;
        candidate.rightsStatus = QStringLiteral("personal_frontend_use_provider_collection");
        const QString repository = QString(providerSystem).replace(' ', '_');
        candidate.downloadUrl = QUrl(QStringLiteral("https://raw.githubusercontent.com/libretro-thumbnails/%1/master/Named_Boxarts/%2.png")
            .arg(repository, encodePathSegment(name)));
        candidate.sourcePage = candidate.downloadUrl;
        CoverScorer::score(candidate, game.region, false, true);
        candidates.append(candidate);
    }
    return candidates;
}

QList<CoverCandidate> LibretroCoverProvider::candidatesForCatalogAssets(const Domain::Game& game, const Domain::System& system,
                                                                          const QString& collectionName,
                                                                          const QStringList& assetPaths) {
    if (game.title.isEmpty() || collectionName.isEmpty()) return {};
    return makeCatalogCandidates(game, system, collectionName, assetPaths);
}

} // namespace LudoShelf::Covers
