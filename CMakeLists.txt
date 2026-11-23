cmake_minimum_required(VERSION 3.16)
project(aether LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra)

find_package(PkgConfig REQUIRED)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WAYLAND_CURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)

find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
set(XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
set(POINTER_GESTURES_XML
    "${WAYLAND_PROTOCOLS_DIR}/unstable/pointer-gestures/pointer-gestures-unstable-v1.xml")

set(GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

add_custom_command(
    OUTPUT "${GEN_DIR}/xdg-shell-protocol.h"
    COMMAND ${WAYLAND_SCANNER} client-header "${XDG_SHELL_XML}" "${GEN_DIR}/xdg-shell-protocol.h"
    DEPENDS "${XDG_SHELL_XML}" VERBATIM)
add_custom_command(
    OUTPUT "${GEN_DIR}/xdg-shell-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${XDG_SHELL_XML}" "${GEN_DIR}/xdg-shell-protocol.c"
    DEPENDS "${XDG_SHELL_XML}" VERBATIM)

add_custom_command(
    OUTPUT "${GEN_DIR}/pointer-gestures-unstable-v1-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} client-header "${POINTER_GESTURES_XML}"
            "${GEN_DIR}/pointer-gestures-unstable-v1-client-protocol.h"
    DEPENDS "${POINTER_GESTURES_XML}" VERBATIM)
add_custom_command(
    OUTPUT "${GEN_DIR}/pointer-gestures-unstable-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code "${POINTER_GESTURES_XML}"
            "${GEN_DIR}/pointer-gestures-unstable-v1-protocol.c"
    DEPENDS "${POINTER_GESTURES_XML}" VERBATIM)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/assets/font-subset.ttf" FONT_HEX HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," FONT_BYTES "${FONT_HEX}")
file(WRITE "${GEN_DIR}/font_data.hpp"
     "// Generated from assets/font-subset.ttf - do not edit.\n"
     "#pragma once\n"
     "static const unsigned char kFontData[] = {${FONT_BYTES}};\n")

option(AETHER_WEBP "Enable WebP support" ON)
option(AETHER_WEBP_SYSTEM "Use the system libwebp instead of downloading it" OFF)

add_library(aether_core STATIC
    src/image/codecs.cpp
    src/image/codec_svg.cpp
    src/image/ops.cpp
    src/image/directory.cpp
    src/text/text.cpp
    src/render/render.cpp)
target_include_directories(aether_core PUBLIC src "${GEN_DIR}" third_party third_party/stb)

if(AETHER_WEBP)
    target_sources(aether_core PRIVATE src/image/codec_webp.cpp)
    target_compile_definitions(aether_core PUBLIC AETHER_WEBP=1)
    if(AETHER_WEBP_SYSTEM)
        pkg_check_modules(LIBWEBP REQUIRED IMPORTED_TARGET libwebp)
        target_link_libraries(aether_core PUBLIC PkgConfig::LIBWEBP)
    else()
        include(FetchContent)
        FetchContent_Declare(libwebp
            URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.6.0.tar.gz
            URL_HASH SHA256=93a852c2b3efafee3723efd4636de855b46f9fe1efddd607e1f42f60fc8f2136)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
        set(WEBP_BUILD_LIBWEBPMUX OFF CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(libwebp)
        target_link_libraries(aether_core PUBLIC webp)
    endif()
endif()

add_executable(aether
    src/main.cpp
    src/app.cpp
    src/platform/connection.cpp
    src/platform/window.cpp
    src/platform/shm_pool.cpp
    src/platform/input.cpp
    src/platform/dnd.cpp
    src/platform/cursors.cpp
    "${GEN_DIR}/xdg-shell-protocol.c"
    "${GEN_DIR}/xdg-shell-protocol.h"
    "${GEN_DIR}/pointer-gestures-unstable-v1-protocol.c"
    "${GEN_DIR}/pointer-gestures-unstable-v1-client-protocol.h")
target_include_directories(aether PRIVATE src "${GEN_DIR}" third_party third_party/stb)
target_link_libraries(aether PRIVATE aether_core PkgConfig::WAYLAND PkgConfig::WAYLAND_CURSOR
                                      PkgConfig::XKBCOMMON)

install(TARGETS aether RUNTIME DESTINATION bin)

enable_testing()
add_executable(aether_tests tests/test_ops.cpp)
target_link_libraries(aether_tests PRIVATE aether_core)
add_test(NAME ops COMMAND aether_tests)
