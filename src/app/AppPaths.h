#ifndef LUDOSHELF_APP_APPPATHS_H
#define LUDOSHELF_APP_APPPATHS_H

#include <QString>

namespace LudoShelf::App {

// All LudoShelf-owned state lives next to the executable so an application
// folder can be moved or copied without leaving state behind in the user's
// profile.
class AppPaths {
public:
    static QString dataRoot();
    static QString databasePath();
    static QString mediaRoot();
    static QString cacheRoot();
    static QString backupsRoot();
    static QString settingsPath();
    // User-supplied controller artwork packaged beside the executable.
    static QString controllerIconsRoot();

    // The prior XDG location is read only for the one-time import.  It is not
    // used for new application state.
    static bool isPortableMode();
    static QString legacyDataRoot();
    static bool migrateLegacyDataIfNeeded();
};

} // namespace LudoShelf::App

#endif // LUDOSHELF_APP_APPPATHS_H
