#include "CoverTypes.h"

namespace LudoShelf::Covers {

namespace {
struct EnumName { CoverKind value; const char *name; };
constexpr EnumName kKinds[] = {
    {CoverKind::BoxFront, "box_front"}, {CoverKind::BoxBack, "box_back"},
    {CoverKind::BoxSpine, "box_spine"}, {CoverKind::BoxFull, "box_full"},
    {CoverKind::BoxThreeDimensional, "box_three_dimensional"}, {CoverKind::Slipcover, "slipcover"},
    {CoverKind::JewelCaseFront, "jewel_case_front"}, {CoverKind::JewelCaseBack, "jewel_case_back"},
    {CoverKind::CartridgeLabel, "cartridge_label"}, {CoverKind::DiscLabel, "disc_label"},
    {CoverKind::CassetteCover, "cassette_cover"}, {CoverKind::FloppyLabel, "floppy_label"},
    {CoverKind::ArcadeFlyerFront, "arcade_flyer_front"}, {CoverKind::ArcadeFlyerBack, "arcade_flyer_back"},
    {CoverKind::ArcadeCabinet, "arcade_cabinet"}, {CoverKind::StoreVerticalCapsule, "store_vertical_capsule"},
    {CoverKind::StoreHorizontalCapsule, "store_horizontal_capsule"},
    {CoverKind::LibraryVerticalArt, "library_vertical_art"}, {CoverKind::PromotionalPoster, "promotional_poster"},
    {CoverKind::FanCreatedCover, "fan_created_cover"}, {CoverKind::GeneratedPlaceholder, "generated_placeholder"},
    {CoverKind::Unknown, "unknown"}
};

struct ScopeName { CoverScope value; const char *name; };
constexpr ScopeName kScopes[] = {
    {CoverScope::GameWork, "game_work"}, {CoverScope::PlatformRelease, "platform_release"},
    {CoverScope::RegionalRelease, "regional_release"}, {CoverScope::Edition, "edition"},
    {CoverScope::Disc, "disc"}, {CoverScope::StoreApplication, "store_application"},
    {CoverScope::ArcadeMachine, "arcade_machine"}, {CoverScope::UserGameRecord, "user_game_record"}
};
}

QString coverKindToString(CoverKind kind) {
    for (const auto& item : kKinds) if (item.value == kind) return QString::fromLatin1(item.name);
    return QStringLiteral("unknown");
}

CoverKind coverKindFromString(const QString& value) {
    for (const auto& item : kKinds) if (value == QLatin1String(item.name)) return item.value;
    return CoverKind::Unknown;
}

QString coverScopeToString(CoverScope scope) {
    for (const auto& item : kScopes) if (item.value == scope) return QString::fromLatin1(item.name);
    return QStringLiteral("game_work");
}

CoverScope coverScopeFromString(const QString& value) {
    for (const auto& item : kScopes) if (value == QLatin1String(item.name)) return item.value;
    return CoverScope::GameWork;
}

bool isPrimaryDisplayCover(CoverKind kind) {
    return kind == CoverKind::BoxFront || kind == CoverKind::JewelCaseFront ||
           kind == CoverKind::ArcadeFlyerFront || kind == CoverKind::LibraryVerticalArt ||
           kind == CoverKind::GeneratedPlaceholder;
}

} // namespace LudoShelf::Covers
