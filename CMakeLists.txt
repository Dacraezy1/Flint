cmake_minimum_required(VERSION 3.22)
project(Flint VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable Qt Creator & other IDEs to see files properly
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find system dependencies
find_package(Qt6 COMPONENTS Widgets Network Concurrent REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(fmt REQUIRED)

# Zstd library finding
find_library(ZSTD_LIBRARY NAMES zstd REQUIRED)
find_path(ZSTD_INCLUDE_DIR NAMES zstd.h REQUIRED)

# Fetch dependencies not present on system
include(FetchContent)

# 1. nlohmann/json
message(STATUS "Fetching nlohmann_json...")
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
)
FetchContent_MakeAvailable(json)

# 2. libzip (minimal build static)
message(STATUS "Fetching libzip...")
FetchContent_Declare(
    libzip
    URL https://github.com/nih-at/libzip/archive/refs/tags/v1.10.1.tar.gz
    URL_HASH SHA256=d56d857d1c3ad4a7f3a4c01a51c6a6e5530e35ab93503f62276e8ba2b306186a
)
# Configure libzip properties before making it available
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")
set(BUILD_TOOLS OFF CACHE INTERNAL "")
set(BUILD_REGRESS OFF CACHE INTERNAL "")
set(BUILD_EXAMPLES OFF CACHE INTERNAL "")
set(BUILD_DOC OFF CACHE INTERNAL "")
set(ENABLE_GNUTLS OFF CACHE INTERNAL "")
set(ENABLE_MBEDTLS OFF CACHE INTERNAL "")
set(ENABLE_COMMONCRYPTO OFF CACHE INTERNAL "")
set(ENABLE_OPENSSL OFF CACHE INTERNAL "")
set(ENABLE_BZIP2 OFF CACHE INTERNAL "")
set(ENABLE_LZMA OFF CACHE INTERNAL "")
set(ENABLE_ZSTD OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(libzip)

# Source files list
set(SOURCES
    src/main.cpp
    src/logging/logging.cpp
    src/filesystem/filesystem.cpp
    src/network/network.cpp
    src/downloads/download_manager.cpp
    src/accounts/accounts.cpp
    src/accounts/microsoft_auth.cpp
    src/java/java_manager.cpp
    src/minecraft/minecraft.cpp
    src/minecraft/instances.cpp
    src/ui/mainwindow.cpp
    src/ui/microsoft_login_dialog.cpp
    src/ui/mod_manager_dialog.cpp
)

add_executable(flint ${SOURCES})

# Target include directories
target_include_directories(flint PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${ZSTD_INCLUDE_DIR}
)

# Target libraries linking
target_link_libraries(flint PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    OpenSSL::SSL
    OpenSSL::Crypto
    fmt::fmt
    ${ZSTD_LIBRARY}
    zip
    nlohmann_json::nlohmann_json
)

# Compiler warnings and options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(flint PRIVATE -Wall -Wextra -Wpedantic)
endif()

# --------------------------------------------------
# Installation Targets
# --------------------------------------------------
install(TARGETS flint DESTINATION bin)
install(FILES flint.desktop DESTINATION share/applications)
install(FILES flint.png DESTINATION share/pixmaps)

# --------------------------------------------------
# CPack Packaging Configuration
# --------------------------------------------------
set(CPACK_PACKAGE_NAME "flint")
set(CPACK_PACKAGE_VERSION "0.1.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Minimal, Linux-native Minecraft Java Edition launcher")
set(CPACK_PACKAGE_VENDOR "Flint Team")
set(CPACK_PACKAGE_CONTACT "younes")
set(CPACK_PACKAGE_LICENSE "GPL-3.0-only")

# Debian/Ubuntu configuration
set(CPACK_DEBIAN_PACKAGE_SECTION "games")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6widgets6, libqt6network6, libqt6concurrent6, libqt6core6")

# RPM/Fedora configuration
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase, qt6-qtbase-gui")

include(CPack)
