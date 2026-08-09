add_library(lzo)
add_library(${PROJECT_NAME}::lzo ALIAS lzo)
target_sources(lzo
    PUBLIC
    FILE_SET HEADERS
    FILES
        lzoconf.h
        lzodefs.h
        minilzo.h
    PRIVATE
        minilzo.c
        testmini.c
)

# TODO install COPYING file
install(FILES COPYING DESTINATION ${CMAKE_INSTALL_DOCDIR}/lzo)