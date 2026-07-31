#ifndef LUDOSHELF_APP_LIBRARYBACKUPSERVICE_H
#define LUDOSHELF_APP_LIBRARYBACKUPSERVICE_H

#include <QString>

namespace LudoShelf::App {

class LibraryBackupService {
public:
    static bool exportLibraryToJson(const QString& targetJsonPath);
    static bool importLibraryFromJson(const QString& sourceJsonPath);
};

} // namespace LudoShelf::App

#endif // LUDOSHELF_APP_LIBRARYBACKUPSERVICE_H
