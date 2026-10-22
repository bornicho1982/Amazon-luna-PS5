# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 3.20)
project(luna_ps5 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opciones de plataforma
option(LUNA_BUILD_PS5 "Compilar como payload / app nativa para PS5" OFF)

if(DEFINED PS5 OR DEFINED PROSPERO OR XCLOUD_PS5)
    set(LUNA_BUILD_PS5 ON)
endif()

# Inclusión de cabeceras de origen
include_directories(src)

# Fuentes comunes del núcleo
set(LUNA_SOURCES
    src/main.cpp
    src/net/http_client.cpp
    src/auth/cbl_client.cpp
    src/session/luna_session.cpp
    src/input/pad_manager.cpp
    src/stream/webrtc_client.cpp
    src/display/font_inter_bold.cpp
    src/display/font_renderer.cpp
    src/display/display_manager.cpp
    src/audio/audio_manager.cpp
    src/media/media_decoder.cpp
    src/util/qrcodegen.c
)

if(LUNA_BUILD_PS5)
    message(STATUS "Configurando compilación nativa para PS5 (x86_64-sie-ps5)")
    add_definitions(-DLUNA_TARGET_PS5=1)

    add_executable(luna_ps5 ${LUNA_SOURCES})
    target_link_libraries(luna_ps5 PRIVATE
        kernel
        SceUserService
        ScePad
        SceAudioOut
        SceVideoOut
        SceNet
        SceSsl
    )
    set_target_properties(luna_ps5 PROPERTIES
        OUTPUT_NAME "luna-ps5.elf"
    )
else()
    message(STATUS "Configurando compilación para Desktop / Simulación")
    add_definitions(-DLUNA_TARGET_DESKTOP=1)

    add_executable(luna_ps5 ${LUNA_SOURCES})
endif()
