add_executable(ludoshelf
    main.cpp
    resources/ludoshelf.qrc
    app/AppPaths.cpp
    app/AppPaths.h
    app/LibraryBackupService.cpp
    app/LibraryBackupService.h
    metadata/RomMetadataTypes.cpp
    metadata/RomMetadataTypes.h
    metadata/RomHashService.cpp
    metadata/RomHashService.h
    metadata/RomMetadataRepository.cpp
    metadata/RomMetadataRepository.h
    metadata/RomMetadataProvider.h
    metadata/LibretroDatabaseProvider.cpp
    metadata/LibretroDatabaseProvider.h
    metadata/LibretroDatabaseBootstrapper.cpp
    metadata/LibretroDatabaseBootstrapper.h
    metadata/RomMetadataCoordinator.cpp
    metadata/RomMetadataCoordinator.h
    covers/CoverTypes.cpp
    covers/CoverTypes.h
    covers/CoverTitleNormalizer.cpp
    covers/CoverTitleNormalizer.h
    covers/CoverScorer.cpp
    covers/CoverScorer.h
    covers/LocalCoverDiscovery.cpp
    covers/LocalCoverDiscovery.h
    covers/RetroArchCoverDiscovery.cpp
    covers/RetroArchCoverDiscovery.h
    covers/LibretroCoverProvider.cpp
    covers/LibretroCoverProvider.h
    covers/LibretroThumbnailCatalog.cpp
    covers/LibretroThumbnailCatalog.h
    covers/CoverAcquisitionService.cpp
    covers/CoverAcquisitionService.h
    dat/DatParser.cpp
    dat/DatParser.h
    dat/HashService.cpp
    dat/HashService.h
    database/DatabaseManager.cpp
    database/DatabaseManager.h
    launch/LaunchService.cpp
    launch/LaunchService.h
    media/MediaStorageManager.cpp
    media/MediaStorageManager.h
    media/PlaceholderGenerator.cpp
    media/PlaceholderGenerator.h
    models/GameFilterProxyModel.cpp
    models/GameFilterProxyModel.h
    models/GameTableModel.cpp
    models/GameTableModel.h
    models/SystemListModel.cpp
    models/SystemListModel.h
    scanning/DirectoryScanner.cpp
    scanning/DirectoryScanner.h
    scanning/DiscGroupDetector.cpp
    scanning/DiscGroupDetector.h
    scanning/FilenameParser.cpp
    scanning/FilenameParser.h
    ui/AddSystemWizard.cpp
    ui/AddSystemWizard.h
    ui/CoverArtDialog.cpp
    ui/CoverArtDialog.h
    ui/ArtworkDelegate.cpp
    ui/ArtworkDelegate.h
    ui/DatAuditDialog.cpp
    ui/DatAuditDialog.h
    ui/DiagnosticsDialog.cpp
    ui/DiagnosticsDialog.h
    ui/EditEmulatorDialog.cpp
    ui/EditEmulatorDialog.h
    ui/EditGameDialog.cpp
    ui/EditGameDialog.h
    ui/EditSystemDialog.cpp
    ui/EditSystemDialog.h
    ui/GameDetailsPanel.cpp
    ui/GameDetailsPanel.h
    ui/GamesGridView.cpp
    ui/GamesGridView.h
    ui/GamesTableView.cpp
    ui/GamesTableView.h
    ui/MainWindow.cpp
    ui/MainWindow.h
    ui/SystemsView.cpp
    ui/SystemsView.h
    ui/SystemPresets.h
    ui/TestLaunchDialog.cpp
    ui/TestLaunchDialog.h
)

find_path(LIBARCHIVE_INCLUDE_DIR archive.h REQUIRED)
find_library(LIBARCHIVE_LIBRARY archive REQUIRED)
find_package(ZLIB REQUIRED)

target_include_directories(ludoshelf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ludoshelf PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Network
    Qt6::Concurrent
    ${LIBARCHIVE_LIBRARY}
    ZLIB::ZLIB
)
target_include_directories(ludoshelf PRIVATE ${LIBARCHIVE_INCLUDE_DIR})

include(FetchContent)

set(LIBRETRO_DATABASE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/libretro-database-1.22.1/rdb")
if(NOT EXISTS "${LIBRETRO_DATABASE_ROOT}")
    message(STATUS "libretro-database not found in build dir, fetching v1.22.1...")
    FetchContent_Declare(
        libretro_database_fetch
        URL https://github.com/libretro/libretro-database/archive/refs/tags/v1.22.1.zip
        URL_HASH SHA256=f98314970f42efdc4bb0d55606b23bec25b74660d1fe8f623eed1aa549e47d12
    )
    FetchContent_MakeAvailable(libretro_database_fetch)
    if(EXISTS "${libretro_database_fetch_SOURCE_DIR}/rdb")
        set(LIBRETRO_DATABASE_ROOT "${libretro_database_fetch_SOURCE_DIR}/rdb")
    else()
        message(FATAL_ERROR "libretro-database-1.22.1/rdb must be present in the build src directory.")
    endif()
endif()

install(TARGETS ludoshelf DESTINATION bin)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/../ludo.png
    DESTINATION share/pixmaps
    RENAME ludoshelf.png
)
install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/resources/ludoshelf.desktop
    DESTINATION share/applications
)
install(DIRECTORY ${LIBRETRO_DATABASE_ROOT}
    DESTINATION bin/libretro-database-1.22.1
)
