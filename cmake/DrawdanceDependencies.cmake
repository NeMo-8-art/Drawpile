# SPDX-License-Identifier: MIT

dp_find_package(ZLIB MODULE REQUIRED)

if(NOT WIN32)
    dp_find_package(Threads REQUIRED)
endif()

if(CLIENT OR TOOLS)
    if(NOT EMSCRIPTEN)
        find_package(PkgConfig QUIET)
        if(PKGCONFIG_FOUND)
            pkg_check_modules(LIBAV IMPORTED_TARGET GLOBAL
                libavcodec
                libavformat
                libavutil
                libswscale
            )
            if(TARGET PkgConfig::LIBAV)
                include(CMakePrintHelpers)
                cmake_print_properties(TARGETS PkgConfig::LIBAV PROPERTIES
                    INTERFACE_COMPILE_DEFINITIONS
                    INTERFACE_COMPILE_FEATURES
                    INTERFACE_COMPILE_OPTIONS
                    INTERFACE_INCLUDE_DIRECTORIES
                    INTERFACE_LINK_DEPENDS
                    INTERFACE_LINK_DIRECTORIES
                    INTERFACE_LINK_LIBRARIES
                    INTERFACE_LINK_LIBRARIES_DIRECT
                    INTERFACE_LINK_LIBRARIES_DIRECT_EXCLUDE
                    INTERFACE_LINK_OPTIONS
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
                )
                add_library(LIBAV::LIBAV ALIAS PkgConfig::LIBAV)
            endif()
        else()
            message(WARNING "PkgConfig NOT FOUND")
        endif()
        add_feature_info("Video export via libav" "TARGET LIBAV::LIBAV" "")
    endif()

    if(IMAGE_IMPL STREQUAL "LIBS")
        dp_find_package(PNG REQUIRED)
        dp_find_package(JPEG REQUIRED)
    elseif(IMAGE_IMPL STREQUAL "QT")
        dp_find_package("Qt${QT_VERSION_MAJOR}" COMPONENTS Gui REQUIRED)
    endif()

    dp_find_package(dpwebp REQUIRED)

    dp_find_package("Qt${QT_VERSION_MAJOR}" COMPONENTS Xml REQUIRED)
    if(ZIP_IMPL STREQUAL "LIBZIP")
        dp_find_package(libzip QUIET)
        if(NOT TARGET libzip::zip)
            # libzip 1.5 has only pkg-config
            if(PKGCONFIG_FOUND)
                pkg_check_modules(libzip REQUIRED IMPORTED_TARGET libzip)
                add_library(libzip::zip ALIAS PkgConfig::libzip)
            endif()
        endif()
    elseif(ZIP_IMPL STREQUAL "KARCHIVE")
        dp_find_package("KF${QT_VERSION_MAJOR}Archive" REQUIRED)
    endif()
endif()

if(USE_GENERATORS)
    dp_find_package("Qt${QT_VERSION_MAJOR}" COMPONENTS Core Gui)
endif()
