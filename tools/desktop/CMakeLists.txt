# Solanum - the app for this computer (macOS or Linux, SDL2 in a window): for working on the interface without a
# console. tools/build-desktop.sh configures it. Its store answers from a folder of fixtures (NP_FIXTURES,
# src/store_desktop.c); nothing is installed or opened. With NP_PREVIEW it draws off screen, plays a button script
# and saves screenshots (NP_SCRIPT, NP_SHOTS), as Mandrake's and Dulcamara's interface previews do.
# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.22)
project(solanum_desktop LANGUAGES C CXX)

set(ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")

find_package(SDL2 CONFIG REQUIRED)
option(NP_PREVIEW "Draw off screen and follow NP_SCRIPT" OFF)

file(GLOB SOURCES "${ROOT}/src/*.c" "${ROOT}/src/ui/*.c")
list(APPEND SOURCES "${ROOT}/src/platform/plat_sdl.c" "${ROOT}/src/platform/keys.c" "${ROOT}/src/platform/xbr.c"
    "${ROOT}/src/platform/blit.c"
    "${ROOT}/src/platform/vk/vk_probe.c")
add_executable(solanum ${SOURCES})
target_include_directories(solanum PRIVATE "${ROOT}/src" "${ROOT}/third_party/stb" "${ROOT}/kit")
target_compile_options(solanum PRIVATE $<$<COMPILE_LANGUAGE:C>:-std=gnu11 -Wall -Wextra>)
if(NP_PREVIEW)
    target_compile_definitions(solanum PRIVATE NP_PREVIEW)
endif()
target_link_libraries(solanum PRIVATE SDL2::SDL2 m pthread)
