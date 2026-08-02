#ifndef LUDOSHELF_APP_LIBRARYBACKUPSERVICE_H
#define LUDOSHELF_APP_LIBRARYBACKUPSERVICE_H

#include <QString>

namespace LudoShelf::App {

class LibraryBackupService {
public:
    static bool exportLibraryToJson(const QString& targetJsonPath);
    // Replaces the library by default.  Merge is intentionally opt-in because
    // it can retain records that are absent from the imported snapshot.
    static bool importLibraryFromJson(const QString& sourceJsonPath, bool replaceExisting = true);
};

} // namespace LudoShelf::App

#endif // LUDOSHELF_APP_LIBRARYBACKUPSERVICE_H
