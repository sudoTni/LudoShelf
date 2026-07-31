#ifndef LUDOSHELF_UI_SYSTEMPRESETS_H
#define LUDOSHELF_UI_SYSTEMPRESETS_H

#include <QList>
#include <QString>
#include <QStringList>

namespace LudoShelf::UI {

struct SystemPreset {
    QString displayName;
    QString shortName;
    QString manufacturer;
    QStringList aliases;
};

inline const QList<SystemPreset>& systemPresets() {
    static const QList<SystemPreset> presets{
        {"Amstrad - CPC", "amstradcpc", "Amstrad", {"amstradcpc", "cpc"}},
        {"Amstrad - GX4000", "gx4000", "Amstrad", {"gx4000"}},
        {"Arduboy Inc - Arduboy", "arduboy", "Arduboy Inc", {"arduboy"}},
        {"Atari - 2600", "atari2600", "Atari", {"atari2600", "a2600", "2600"}},
        {"Atari - 5200", "atari5200", "Atari", {"atari5200", "a5200", "5200"}},
        {"Atari - 7800", "atari7800", "Atari", {"atari7800", "a7800", "7800"}},
        {"Atari - 8-bit Family", "atari8bit", "Atari", {"atari8bit", "atari800", "a8"}},
        {"Atari - Jaguar", "atarijaguar", "Atari", {"atarijaguar", "jaguar"}},
        {"Atari - Lynx", "atarilynx", "Atari", {"atarilynx", "lynx"}},
        {"Atari - ST", "atarist", "Atari", {"atarist", "st"}},
        {"Atomiswave", "atomiswave", "Sammy", {"atomiswave"}},
        {"Bandai - WonderSwan Color", "wonderswancolor", "Bandai", {"wonderswancolor", "wsc"}},
        {"Bandai - WonderSwan", "wonderswan", "Bandai", {"wonderswan", "ws"}},
        {"Casio - Loopy", "casioloopy", "Casio", {"casioloopy", "loopy"}},
        {"Casio - PV-1000", "pv1000", "Casio", {"pv1000"}},
        {"Coleco - ColecoVision", "colecovision", "Coleco", {"colecovision", "cv"}},
        {"Commodore - 64", "c64", "Commodore", {"c64", "commodore64"}},
        {"Commodore - Amiga", "amiga", "Commodore", {"amiga"}},
        {"Commodore - CD32", "cd32", "Commodore", {"cd32", "amigacd32"}},
        {"Commodore - CDTV", "cdtv", "Commodore", {"cdtv", "amigacdtv"}},
        {"Commodore - PET", "pet", "Commodore", {"pet", "commodorepet"}},
        {"Commodore - Plus-4", "plus4", "Commodore", {"plus4", "commodoreplus4"}},
        {"Commodore - VIC-20", "vic20", "Commodore", {"vic20", "commodorevic20"}},
        {"Emerson - Arcadia 2001", "arcadia2001", "Emerson", {"arcadia2001"}},
        {"Enterprise - 128", "enterprise128", "Enterprise", {"enterprise128"}},
        {"Entex - Adventure Vision", "adventurevision", "Entex", {"adventurevision"}},
        {"Epoch - Super Cassette Vision", "supercassettevision", "Epoch", {"supercassettevision", "scv"}},
        {"Fairchild - Channel F", "channelf", "Fairchild", {"channelf", "fairchildchannelf"}},
        {"Funtech - Super Acan", "superacan", "Funtech", {"superacan"}},
        {"GamePark - GP32", "gp32", "GamePark", {"gp32"}},
        {"GCE - Vectrex", "vectrex", "GCE", {"vectrex"}},
        {"Handheld Electronic Game", "handheld", "", {"handheld", "electronicgame"}},
        {"Hartung - Game Master", "gamemaster", "Hartung", {"gamemaster"}},
        {"LeapFrog - Leapster Learning Game System", "leapster", "LeapFrog", {"leapster"}},
        {"Magnavox - Odyssey2", "odyssey2", "Magnavox", {"odyssey2", "o2"}},
        {"Mattel - Intellivision", "intellivision", "Mattel", {"intellivision", "intv"}},
        {"Microsoft - MSX2", "msx2", "Microsoft", {"msx2"}},
        {"Microsoft - MSX", "msx", "Microsoft", {"msx"}},
        {"Microsoft - Xbox", "xbox", "Microsoft", {"xbox"}},
        {"NEC - PC-8001 - PC-8801", "pc88", "NEC", {"pc88", "pc8001", "pc8801"}},
        {"NEC - PC-98", "pc98", "NEC", {"pc98"}},
        {"NEC - PC Engine CD - TurboGrafx-CD", "pcecd", "NEC", {"pcecd", "tgcd", "turbografxcd"}},
        {"NEC - PC Engine SuperGrafx", "supergrafx", "NEC", {"supergrafx"}},
        {"NEC - PC Engine - TurboGrafx 16", "pcengine", "NEC", {"pcengine", "tg16", "turbografx16"}},
        {"NEC - PC-FX", "pcfx", "NEC", {"pcfx"}},
        {"Nintendo - e-Reader", "ereader", "Nintendo", {"ereader"}},
        {"Nintendo - Family Computer Disk System", "fds", "Nintendo", {"fds", "famicomdisksystem"}},
        {"Nintendo - Game Boy Advance", "gba", "Nintendo", {"gba", "gameboyadvance"}},
        {"Nintendo - Game Boy Color", "gbc", "Nintendo", {"gbc", "gameboycolor"}},
        {"Nintendo - Game Boy", "gb", "Nintendo", {"gb", "gameboy"}},
        {"Nintendo - GameCube", "gamecube", "Nintendo", {"gc", "gcn", "gamecube", "nintendogamecube", "nintendogamecb"}},
        {"Nintendo - Nintendo 3DS", "3ds", "Nintendo", {"3ds", "nintendo3ds"}},
        {"Nintendo - Nintendo 64DD", "n64dd", "Nintendo", {"n64dd", "nintendo64dd"}},
        {"Nintendo - Nintendo 64", "nintendo64", "Nintendo", {"n64", "nintendo64"}},
        {"Nintendo - Nintendo DSi", "dsi", "Nintendo", {"dsi", "nintendodsi"}},
        {"Nintendo - Nintendo DS", "nds", "Nintendo", {"nds", "nintendods"}},
        {"Nintendo - Nintendo Entertainment System", "nintendo", "Nintendo", {"nes", "nintendo", "nintendones"}},
        {"Nintendo - Pokemon Mini", "pokemonmini", "Nintendo", {"pokemonmini"}},
        {"Nintendo - Satellaview", "satellaview", "Nintendo", {"satellaview", "bsx"}},
        {"Nintendo - Sufami Turbo", "sufamiturbo", "Nintendo", {"sufamiturbo"}},
        {"Nintendo - Super Nintendo Entertainment System", "supernintendo", "Nintendo", {"snes", "supernes", "supernintendo"}},
        {"Nintendo - Virtual Boy", "virtualboy", "Nintendo", {"virtualboy", "vb"}},
        {"Nintendo - Wii", "wii", "Nintendo", {"wii"}},
        {"Philips - CD-i", "cdi", "Philips", {"cdi", "cd-i"}},
        {"Philips - Videopac+", "videopac", "Philips", {"videopac", "videopacplus"}},
        {"RCA - Studio II", "studio2", "RCA", {"studio2"}},
        {"Sega - 32X", "32x", "Sega", {"32x", "sega32x"}},
        {"Sega - Dreamcast", "dreamcast", "Sega", {"dc", "dreamcast"}},
        {"Sega - Game Gear", "gamegear", "Sega", {"gamegear", "gg"}},
        {"Sega - Master System - Mark III", "mastersystem", "Sega", {"mastersystem", "markiii", "sms"}},
        {"Sega - Mega-CD - Sega CD", "segacd", "Sega", {"segacd", "megacd", "scd"}},
        {"Sega - Mega Drive - Genesis", "segagenesis", "Sega", {"genesis", "megadrive", "md", "segagenesis", "segamegadrive"}},
        {"Sega - Naomi 2", "naomi2", "Sega", {"naomi2"}},
        {"Sega - Naomi", "naomi", "Sega", {"naomi"}},
        {"Sega - PICO", "pico", "Sega", {"pico", "segapico"}},
        {"Sega - Saturn", "saturn", "Sega", {"saturn"}},
        {"Sega - SG-1000", "sg1000", "Sega", {"sg1000"}},
        {"Sharp - X1", "sharpX1", "Sharp", {"sharpx1", "x1"}},
        {"Sharp - X68000", "x68000", "Sharp", {"x68000", "sharpX68000"}},
        {"Sinclair - ZX 81", "zx81", "Sinclair", {"zx81"}},
        {"Sinclair - ZX Spectrum +3", "zxspectrum3", "Sinclair", {"zxspectrum3", "zxspectrumplus3"}},
        {"Sinclair - ZX Spectrum", "zxspectrum", "Sinclair", {"zxspectrum", "spectrum"}},
        {"SNK - Neo Geo CD", "neogeocd", "SNK", {"neogeocd"}},
        {"SNK - Neo Geo Pocket Color", "neogeopocketcolor", "SNK", {"neogeopocketcolor", "ngpc"}},
        {"SNK - Neo Geo Pocket", "neogeopocket", "SNK", {"neogeopocket", "ngp"}},
        {"SNK - Neo Geo", "neogeo", "SNK", {"neogeo", "ng"}},
        {"Sony - PlayStation 2", "ps2", "Sony", {"ps2", "playstation2"}},
        {"Sony - PlayStation 3", "ps3", "Sony", {"ps3", "playstation3"}},
        {"Sony - PlayStation Portable", "psp", "Sony", {"psp", "playstationportable"}},
        {"Sony - PlayStation", "playstation", "Sony", {"psx", "ps1", "playstation"}},
        {"Sony - PlayStation Vita", "psvita", "Sony", {"psvita", "vita", "playstationvita"}},
        {"Spectravideo - SVI-318 - SVI-328", "svi", "Spectravideo", {"svi", "svi318", "svi328"}},
        {"The 3DO Company - 3DO", "3do", "The 3DO Company", {"3do"}},
        {"Thomson - MOTO", "thomsonmoto", "Thomson", {"thomsonmoto", "moto"}},
        {"Tiger - Game.com", "gamecom", "Tiger", {"gamecom"}},
        {"Uzebox", "uzebox", "Uzebox", {"uzebox"}},
        {"Videoton - TV-Computer", "tvc", "Videoton", {"tvc", "tvcomputer"}},
        {"VTech - CreatiVision", "creativision", "VTech", {"creativision"}},
        {"VTech - V.Smile", "vsmile", "VTech", {"vsmile"}},
        {"Watara - Supervision", "supervision", "Watara", {"supervision"}},
    };
    return presets;
}

inline int matchingSystemPreset(const QString& name, const QString& shortName) {
    const QString normalizedName = name.trimmed().toCaseFolded();
    const QString normalizedShortName = shortName.trimmed().toCaseFolded();
    const auto& presets = systemPresets();
    for (qsizetype index = 0; index < presets.size(); ++index) {
        const SystemPreset& preset = presets.at(index);
        if (normalizedName == preset.displayName.toCaseFolded() ||
            normalizedShortName == preset.shortName.toCaseFolded()) return static_cast<int>(index);
    }
    return -1;
}

} // namespace LudoShelf::UI

#endif // LUDOSHELF_UI_SYSTEMPRESETS_H
