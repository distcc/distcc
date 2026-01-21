function(create_config TARGET_NAME)
   
    # List of headers to check
    set(HEADERS
        alloca.h
        arpa/nameser.h
        ctype.h
        float.h
        inttypes.h
        mcheck.h
        memory.h
        netinet/in.h
        resolv.h
        stdint.h
        stdlib.h
        strings.h
        string.h
        sys/mman.h
        sys/resource.h
        sys/select.h
        sys/sendfile.h
        sys/signal.h
        sys/socket.h
        sys/stat.h
        sys/types.h
        unistd.h
    )

    # Check for headers
    include(CheckIncludeFiles)
    foreach(header ${HEADERS})
        # TODO consider collapsing the string replace to [/.]
        string(REPLACE "/" "_" header_var ${header})
        string(REPLACE "." "_" header_var ${header_var})
        string(TOUPPER "${header_var}" HEADER_UPPER)
        check_include_files(${header} HAVE_${HEADER_UPPER})
    endforeach()

    # List of functions to check
    set(FUNCTIONS
        asprintf
        flock
        getaddrinfo
        getcwd
        geteuid
        getloadavg
        getnameinfo
        getpagesize
        getrusage
        gettimeofday
        getuid
        getwd
        hstrerror
        inet_ntoa
        inet_ntop
        lockf
        mcheck
        mkdtemp
        mmap
        sendfile
        setgroups
        setreuid
        setsid
        setuid
        snprintf
        socketpair
        strerror
        strlcpy
        strndup
        strsep
        strsignal
        vasprintf
        vsnprintf
        wait3
        wait4
        waitpid
    )

    # Check for functions
    include(CheckFunctionExists)
    foreach(func ${FUNCTIONS})
        string(TOUPPER "${func}" FUNC_UPPER)
        # TODO the CMake docs recommend check_symbol_exists entirely.
        # https://cmake.org/cmake/help/latest/module/CheckFunctionExists.html#command:check_function_exists
        # Consider migrating
        check_function_exists(${func} HAVE_${FUNC_UPPER})
    endforeach()

    # Some symbols seem to require special hints to be resolved.
    # Macros like VA_COPY must be found this way.
    include(CheckSymbolExists)

    check_symbol_exists(va_copy "stdarg.h" HAVE_VA_COPY)

    # Check for the type in_port_t
    check_symbol_exists(in_port_t "netinet/in.h" HAVE_IN_PORT_T)

    # Check for the type in_addr_t
    check_symbol_exists(in_addr_t "netinet/in.h" HAVE_IN_ADDR_T)

    # Check for the type sockaddr_storage
    include(CheckStructHasMember)
    check_struct_has_member("struct sockaddr_storage" ss_family "sys/socket.h" HAVE_SOCKADDR_STORAGE)

    # Set the compiler triple (architecture-system)
    set(NATIVE_COMPILER_TRIPLE "${CMAKE_SYSTEM_PROCESSOR}-${CMAKE_SYSTEM_NAME}")

    include(GNUInstallDirs)
    set(LIBDIR "${CMAKE_INSTALL_FULL_LIBDIR}")

    # Generate config.h from config.h.in
    configure_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.h.in
        ${CMAKE_BINARY_DIR}/config.h
        @ONLY
    )

    # Ensure config.h can be included elsewhere
    target_include_directories(${TARGET_NAME} PUBLIC ${CMAKE_BINARY_DIR})
endfunction()