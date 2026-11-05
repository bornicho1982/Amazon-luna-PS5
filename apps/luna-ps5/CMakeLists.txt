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

# Fuentes comunes del núcleo. Esta lista debe mantenerse alineada con
# apps/luna-ps5/build_ps5.ps1 (la app real de PS5 se compila con ese script).
set(LUNA_SOURCES
    src/net/http_client.cpp
    src/auth/cbl_client.cpp
    src/auth/auth_server.cpp
    src/session/luna_session.cpp
    src/input/pad_manager.cpp
    src/stream/webrtc_client.cpp
    src/display/font_inter_bold.cpp
    src/display/font_renderer.cpp
    src/display/display_manager.cpp
    src/display/ps5_controller_icons.cpp
    src/display/clay_renderer.cpp
    src/display/luna_clay_ui.cpp
    src/display/hero_banner_asset.cpp
    src/audio/audio_manager.cpp
    src/media/media_decoder.cpp
    src/media/mp4_preview_player.cpp
    src/util/crash_logger.cpp
    src/util/image_loader.cpp
    src/util/settings.cpp
    src/util/qrcodegen.c
)

# Decodificador AAC de Helix (C puro, usado por Mp4PreviewPlayer).
file(GLOB LUNA_HELIX_AAC_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/media/helix_aac/*.c")
list(APPEND LUNA_SOURCES ${LUNA_HELIX_AAC_SOURCES})

set_source_files_properties(${LUNA_HELIX_AAC_SOURCES} PROPERTIES
    COMPILE_DEFINITIONS "USE_DEFAULT_STDLIB")

set(LUNA_MAIN_SOURCE src/main.cpp)

if(LUNA_BUILD_PS5)
    message(STATUS "Configurando compilación nativa para PS5 (x86_64-sie-ps5)")
    add_definitions(-DLUNA_TARGET_PS5=1)

    add_library(luna_core STATIC ${LUNA_SOURCES})
    target_include_directories(luna_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/media/helix_aac
    )

    add_executable(luna_ps5 ${LUNA_MAIN_SOURCE})
    target_link_libraries(luna_ps5 PRIVATE
        luna_core
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

    # ---------------------------------------------------------------------
    # libdatachannel + mbedTLS
    #
    # El proyecto PS5 ya trae las fuentes en tools/reference (ignorado por
    # git). Las usamos también en escritorio para compilar exactamente la
    # misma versión de la librería que se enlaza en PS5.
    # ---------------------------------------------------------------------
    set(LUNA_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/reference/xbox-cloud-gaming-ps5/deps")
    set(LIBDATACHANNEL_DIR "${LUNA_DEPS_DIR}/libdatachannel")
    set(MBEDTLS_DIR "${LUNA_DEPS_DIR}/mbedtls")

    if(EXISTS "${LIBDATACHANNEL_DIR}/CMakeLists.txt" AND EXISTS "${MBEDTLS_DIR}/CMakeLists.txt")
        message(STATUS "Usando libdatachannel + mbedTLS locales: ${LUNA_DEPS_DIR}")

        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static libraries" FORCE)
        set(ENABLE_PROGRAMS OFF CACHE BOOL "Build mbedTLS programs" FORCE)
        set(ENABLE_TESTING OFF CACHE BOOL "Build mbedTLS tests" FORCE)
        set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "Treat mbedTLS warnings as errors" FORCE)
        set(USE_MBEDTLS ON CACHE BOOL "Use MbedTLS instead of OpenSSL" FORCE)
        set(ENABLE_MBEDTLS ON CACHE BOOL "Enable MbedTLS crypto engine for libSRTP" FORCE)
        set(NO_EXAMPLES ON CACHE BOOL "Disable libdatachannel examples" FORCE)
        set(NO_TESTS ON CACHE BOOL "Disable libdatachannel tests" FORCE)
        set(NO_WEBSOCKET ON CACHE BOOL "Disable libdatachannel WebSocket support" FORCE)

        # No instalamos nada: evitamos que las reglas install(EXPORT) de
        # libdatachannel/libSRTP exijan exportar también los targets de mbedTLS.
        set(CMAKE_SKIP_INSTALL_RULES ON)

        # mbedTLS no exporta un target "MbedTLS::MbedTLS" al usarse con
        # add_subdirectory(). Creamos un bundle equivalente para que
        # libdatachannel no intente find_package(MbedTLS).
        add_subdirectory("${MBEDTLS_DIR}" "${CMAKE_BINARY_DIR}/_deps/mbedtls" EXCLUDE_FROM_ALL)

        # Activa DTLS-SRTP mediante un overlay, sin editar el config
        # vendorizado. mbedTLS lo necesita al compilarse y libdatachannel al
        # incluir sus cabeceras (las declaraciones estan bajo ese #define).
        set(LUNA_MBEDTLS_USER_CONFIG "luna_mbedtls_user_config.h")
        set(LUNA_MBEDTLS_USER_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

        if(TARGET mbedtls)
            target_compile_definitions(mbedtls PRIVATE
                MBEDTLS_USER_CONFIG_FILE="${LUNA_MBEDTLS_USER_CONFIG}")
            target_include_directories(mbedtls PRIVATE "${LUNA_MBEDTLS_USER_CONFIG_DIR}")
        endif()

        if(NOT TARGET MbedTLS::MbedTLS)
            add_library(MbedTLS::MbedTLS INTERFACE IMPORTED GLOBAL)
            set_target_properties(MbedTLS::MbedTLS PROPERTIES
                INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_DIR}/include;${LUNA_MBEDTLS_USER_CONFIG_DIR}"
                INTERFACE_COMPILE_DEFINITIONS "MBEDTLS_USER_CONFIG_FILE=\"${LUNA_MBEDTLS_USER_CONFIG}\""
                INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto"
            )
        endif()

        # libSRTP (submódulo de libdatachannel) busca MbedTLS con
        # find_package(). Le damos un módulo que reutiliza los targets ya
        # creados por add_subdirectory(mbedtls).
        list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

        add_subdirectory("${LIBDATACHANNEL_DIR}" "${CMAKE_BINARY_DIR}/_deps/libdatachannel" EXCLUDE_FROM_ALL)
        set(LUNA_RTC_TARGET datachannel-static)
    else()
        message(STATUS "libdatachannel local no encontrado; buscando instalación del sistema")
        find_package(LibDataChannel CONFIG REQUIRED)
        set(LUNA_RTC_TARGET LibDataChannel::LibDataChannel)
    endif()

    add_library(luna_core STATIC ${LUNA_SOURCES})
    target_include_directories(luna_core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/media/helix_aac
    )
    target_link_libraries(luna_core PUBLIC ${LUNA_RTC_TARGET})
    if(WIN32)
        target_link_libraries(luna_core PUBLIC ws2_32)
        # Evita que windows.h defina min/max como macros y rompa std::min.
        target_compile_definitions(luna_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()

    if(MSVC)
        target_compile_options(luna_core PRIVATE /utf-8 /bigobj)
    endif()

    add_executable(luna_ps5 ${LUNA_MAIN_SOURCE})
    target_link_libraries(luna_ps5 PRIVATE luna_core)
    if(MSVC)
        target_compile_options(luna_ps5 PRIVATE /utf-8)
    endif()

    # Tests unitarios
    enable_testing()
    add_subdirectory(tests/unit)
endif()
