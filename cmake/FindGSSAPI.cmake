# FindGSSAPI.cmake - Locate the GSSAPI (Generic Security Services API) libraries and headers

# Search for the GSSAPI include directory
find_path(GSSAPI_INCLUDE_DIR
    NAMES gssapi/gssapi.h gssapi/gssapi_krb5.h
    PATHS
        /usr/include
        /usr/include/gssapi
        /usr/include/mit-krb5
        /usr/include/heimdal
    NO_DEFAULT_PATH
)

# Search for the GSSAPI library
find_library(GSSAPI_LIBRARY
    NAMES gssapi_krb5 gssapi
    PATHS
        /usr/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib64
        /lib
    NO_DEFAULT_PATH
)

# Handle required dependencies
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(GSSAPI
    REQUIRED_VARS GSSAPI_LIBRARY GSSAPI_INCLUDE_DIR
)

# If found, create an imported target
if(GSSAPI_FOUND)
    add_library(GSSAPI::GSSAPI INTERFACE IMPORTED)
    set_target_properties(GSSAPI::GSSAPI PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${GSSAPI_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${GSSAPI_LIBRARY}"
    )
endif()

# Provide output variables
mark_as_advanced(GSSAPI_INCLUDE_DIR GSSAPI_LIBRARY)
