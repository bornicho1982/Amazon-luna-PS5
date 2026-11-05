# FindMbedTLS.cmake
#
# libdatachannel's bundled libSRTP calls find_package(MbedTLS REQUIRED).
# When mbedTLS is added with add_subdirectory() there is no installed
# MbedTLSConfig.cmake, so this module reuses the targets that already exist
# in the build tree. It intentionally avoids searching the system.

if(TARGET mbedtls AND TARGET mbedx509 AND TARGET mbedcrypto)
    set(MbedTLS_FOUND TRUE)
    set(MBEDTLS_FOUND TRUE)

    if(DEFINED MBEDTLS_DIR)
        set(MBEDTLS_INCLUDE_DIRS "${MBEDTLS_DIR}/include")
    else()
        get_target_property(MBEDTLS_INCLUDE_DIRS mbedtls INTERFACE_INCLUDE_DIRECTORIES)
    endif()

    set(MBEDTLS_LIBRARIES mbedtls mbedx509 mbedcrypto)

    if(NOT TARGET MbedTLS::MbedTLS)
        add_library(MbedTLS::MbedTLS INTERFACE IMPORTED)
        set_target_properties(MbedTLS::MbedTLS PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "mbedtls;mbedx509;mbedcrypto"
        )
    endif()

    set(MbedTLS_VERSION_STRING "3.6.2")
    return()
endif()

# Fallback for a normal system installation.
find_path(MBEDTLS_INCLUDE_DIRS NAMES mbedtls/ssl.h)
find_library(MBEDTLS_LIBRARIES NAMES mbedtls)
find_library(MBEDX509_LIBRARIES NAMES mbedx509)
find_library(MBEDCRYPTO_LIBRARIES NAMES mbedcrypto)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MbedTLS
    DEFAULT_MSG
    MBEDTLS_INCLUDE_DIRS
    MBEDTLS_LIBRARIES
)

if(MbedTLS_FOUND)
    if(NOT TARGET MbedTLS::MbedTLS)
        add_library(MbedTLS::MbedTLS INTERFACE IMPORTED)
        set_target_properties(MbedTLS::MbedTLS PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${MBEDTLS_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${MBEDTLS_LIBRARIES};${MBEDX509_LIBRARIES};${MBEDCRYPTO_LIBRARIES}"
        )
    endif()
    set(MBEDTLS_LIBRARIES "${MBEDTLS_LIBRARIES};${MBEDX509_LIBRARIES};${MBEDCRYPTO_LIBRARIES}")
endif()
