add_executable(test_filename_parser
    test_filename_parser.cpp
    ../src/scanning/FilenameParser.cpp
    ../src/covers/CoverTitleNormalizer.cpp
)
target_include_directories(test_filename_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(test_filename_parser PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_filename_parser COMMAND test_filename_parser)

add_executable(test_disc_group_detector
    test_disc_group_detector.cpp
    ../src/scanning/DiscGroupDetector.cpp
)
target_include_directories(test_disc_group_detector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(test_disc_group_detector PRIVATE Qt6::Core Qt6::Test)
add_test(NAME test_disc_group_detector COMMAND test_disc_group_detector)

add_executable(test_launch_service
    test_launch_service.cpp
    ../src/launch/LaunchService.cpp
)
target_include_directories(test_launch_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(test_launch_service PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME test_launch_service COMMAND test_launch_service)

add_executable(test_media_storage
    test_media_storage.cpp
    ../src/app/AppPaths.cpp
    ../src/covers/CoverTypes.cpp
    ../src/media/MediaStorageManager.cpp
    ../src/media/PlaceholderGenerator.cpp
    ../src/models/GameTableModel.cpp
    ../src/database/DatabaseManager.cpp
)
target_include_directories(test_media_storage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(test_media_storage PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Test)
add_test(NAME test_media_storage COMMAND test_media_storage)

add_executable(test_cover_foundation
    test_cover_foundation.cpp
    ../src/app/AppPaths.cpp
    ../src/covers/CoverTypes.cpp
    ../src/covers/CoverTitleNormalizer.cpp
    ../src/covers/CoverScorer.cpp
    ../src/covers/LocalCoverDiscovery.cpp
    ../src/covers/RetroArchCoverDiscovery.cpp
    ../src/covers/LibretroCoverProvider.cpp
    ../src/covers/LibretroThumbnailCatalog.cpp
    ../src/database/DatabaseManager.cpp
)
target_include_directories(test_cover_foundation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(test_cover_foundation PRIVATE Qt6::Core Qt6::Gui Qt6::Sql Qt6::Network Qt6::Concurrent Qt6::Test)
add_test(NAME test_cover_foundation COMMAND test_cover_foundation)

find_path(LIBARCHIVE_INCLUDE_DIR archive.h REQUIRED)
find_library(LIBARCHIVE_LIBRARY archive REQUIRED)
find_package(ZLIB REQUIRED)
add_executable(test_rom_metadata
    test_rom_metadata.cpp
    ../src/metadata/RomMetadataTypes.cpp
    ../src/metadata/RomHashService.cpp
    ../src/metadata/RomMetadataProvider.h
    ../src/metadata/LibretroDatabaseProvider.cpp
)
target_include_directories(test_rom_metadata PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src ${LIBARCHIVE_INCLUDE_DIR})
target_link_libraries(test_rom_metadata PRIVATE Qt6::Core Qt6::Concurrent Qt6::Test ${LIBARCHIVE_LIBRARY} ZLIB::ZLIB)
target_compile_definitions(test_rom_metadata PRIVATE LUDOSHELF_TEST_LIBRETRO_DATABASE_ROOT="${CMAKE_BINARY_DIR}/src/libretro-database-1.22.1/rdb")
add_test(NAME test_rom_metadata COMMAND test_rom_metadata)
