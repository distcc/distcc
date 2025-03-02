cmake_minimum_required(VERSION 3.23)
project(distcc LANGUAGES C)

find_package(PkgConfig REQUIRED)
pkg_check_modules(POPT REQUIRED IMPORTED_TARGET popt)

# TODO
# option(WITH_GNOME "Build GNOME-based monitor" OFF)

# libgtk-3-dev only. libgtk-4-dev won't work
option(WITH_GTK "Build GTK+-based monitor" OFF)
if(WITH_GTK)
    # TODO support multiple gtk versions
    # pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk3)
    pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
endif()

# libavahi-common-dev, libavahi-client-dev
option(WITH_AVAHI "Build with Avahi" ON)
if(WITH_AVAHI)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
    find_package(Avahi REQUIRED)
    set(HAVE_AVAHI ON)
else()
    set(HAVE_AVAHI OFF)
endif()
option(WITH_AUTH "Provide mutual authentication services via GSS-API" OFF)

add_library(${PROJECT_NAME}_lib)
add_library(${PROJECT_NAME}::${PROJECT_NAME}_lib ALIAS ${PROJECT_NAME}_lib)

# Set SYSCONFDIR to a standard path, but allow chancing it
include(GNUInstallDirs)
set(SYSCONFDIR "${CMAKE_INSTALL_SYSCONFDIR}" CACHE PATH "System-wide configuration directory")
set(ICONDIR "${CMAKE_INSTALL_DATAROOTDIR}/pixmaps")

# Include config generation script
include(${CMAKE_SOURCE_DIR}/cmake/create_config.cmake)
# Generate config.h for distcc
create_config(${PROJECT_NAME}_lib)

add_subdirectory(src)
add_subdirectory(lzo)
add_subdirectory(gnome)

target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC
        ${PROJECT_NAME}::lzo
        PkgConfig::POPT
)

install(TARGETS ${PROJECT_NAME})

install(FILES COPYING DESTINATION ${CMAKE_INSTALL_DOCDIR})
# TODO install COPYING file