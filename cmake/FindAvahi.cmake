# FindAvahi.cmake - Locate the Avahi libraries and headers

# Search for the Avahi include directory
find_path(AVAHI_COMMON_INCLUDE_DIR
    NAMES avahi-common/address.h
    PATHS
        /usr/include
        /usr/include/avahi-common
    NO_DEFAULT_PATH
)

find_path(AVAHI_CLIENT_INCLUDE_DIR
    NAMES avahi-client/publish.h
    PATHS
        /usr/include
        /usr/include/avahi-client
    NO_DEFAULT_PATH
)

# Search for the Avahi library
find_library(AVAHI_COMMON_LIBRARY
    NAMES avahi-common
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
    NO_DEFAULT_PATH
)

find_library(AVAHI_CLIENT_LIBRARY
    NAMES avahi-client
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
    NO_DEFAULT_PATH
)

# Handle required dependencies
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Avahi
    REQUIRED_VARS AVAHI_COMMON_LIBRARY AVAHI_COMMON_INCLUDE_DIR AVAHI_CLIENT_LIBRARY AVAHI_CLIENT_INCLUDE_DIR
)

# If found, create an imported target
if(AVAHI_FOUND)
    add_library(Avahi::Avahi INTERFACE IMPORTED)
    set_target_properties(Avahi::Avahi PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${AVAHI_COMMON_INCLUDE_DIR};${AVAHI_CLIENT_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${AVAHI_COMMON_LIBRARY};${AVAHI_CLIENT_LIBRARY}"
    )
endif()

# Provide output variables
mark_as_advanced(AVAHI_COMMON_INCLUDE_DIR AVAHI_CLIENT_INCLUDE_DIR AVAHI_COMMON_LIBRARY  AVAHI_CLIENT_LIBRARY)
