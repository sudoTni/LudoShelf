cmake_minimum_required(VERSION 3.16)

project(ludoshelf
    VERSION 0.1.0
    DESCRIPTION "Qt Game Library and Emulator Frontend"
    HOMEPAGE_URL "https://github.com/ludoshelf/ludoshelf"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Qt MOC/UIC/RCC processing
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Enable compiler warnings
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()

# Find Qt6 modules
find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Sql
    Network
    Concurrent
    Test
)

add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
