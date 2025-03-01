cmake_minimum_required(VERSION 3.23)
project(distcc LANGUAGES C)

# TODO
# * Allow removing compression with LZO at build time
# * Expose the same arguments as ./configure --help for the optional features

find_package(PkgConfig REQUIRED)
pkg_check_modules(POPT REQUIRED IMPORTED_TARGET popt)

# TODO avahi with libavahi-common-dev to set HAVE_AVAHI

add_library(${PROJECT_NAME}_lib)
add_library(${PROJECT_NAME}::${PROJECT_NAME}_lib ALIAS ${PROJECT_NAME}_lib)

# Set SYSCONFDIR to a standard path (default to /etc)
set(SYSCONFDIR "/etc" CACHE PATH "System-wide configuration directory")

# Include config generation script
include(${CMAKE_SOURCE_DIR}/cmake/create_config.cmake)
# Generate config.h for distcc
create_config(${PROJECT_NAME}_lib)

add_subdirectory(src)
add_subdirectory(lzo)

target_link_libraries(${PROJECT_NAME}_lib
    PUBLIC
        ${PROJECT_NAME}::lzo
        PkgConfig::POPT
)
