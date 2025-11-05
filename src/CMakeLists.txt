
if(HAVE_AVAHI)
    list(APPEND ZEROCONF_SOURCES zeroconf.c zeroconf-reg.c)
else()
    set(ZEROCONF_SOURCES)
endif()

if(HAVE_GSSAPI)
    list(APPEND AUTH_SOURCES auth_common.c auth_distcc.c auth_distccd.c)
else()
    set(AUTH_SOURCES)
endif()

target_sources(${PROJECT_NAME}_lib
    PUBLIC
        distcc.h
    PRIVATE
        access.c
        arg.c
        argutil.c
        backoff.c
        bulk.c
        cleanup.c
        climasq.c
        clinet.c
        clirpc.c
        compile.c
        compress.c
        cpp.c
        daemon.c
        distcc.c
        dopt.c
        dotd.c
        dparent.c
        dsignal.c
        emaillog.c
        exec.c
        filename.c
        fix_debug_info.c
        gcc-id.c
        h_argvtostr.c
        h_compile.c
        h_dotd.c
        help.c
        h_exten.c
        h_getline.c
        h_hosts.c
        h_issource.c
        history.c
        hostfile.c
        hosts.c
        h_parsemask.c
        h_sa2str.c
        h_scanargs.c
        h_strip.c
        implicit.c
        include_server_if.c
        io.c
        loadfile.c
        lock.c
        lsdistcc.c
        ncpus.c
        netutil.c
        prefork.c
        pump.c
        remote.c
        rpc.c
        rslave.c
        safeguard.c
        sendfile.c
        serve.c
        setuid.c
        snprintf.c
        srvnet.c
        srvrpc.c
        ssh.c
        state.c
        stats.c
        stringmap.c
        strip.c
        tempfile.c
        timefile.c
        timeval.c
        trace.c
        traceenv.c
        util.c
        where.c
        ${ZEROCONF_SOURCES}
        ${AUTH_SOURCES}
)

target_link_libraries(${PROJECT_NAME}_lib
    PRIVATE
        $<$<BOOL:${HAVE_AVAHI}>:Avahi::Avahi>
        $<$<BOOL:${HAVE_GSSAPI}>:GSSAPI::GSSAPI>
)

add_executable(${PROJECT_NAME})

target_sources(${PROJECT_NAME}
    PRIVATE
        distcc.c
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${PROJECT_NAME}::${PROJECT_NAME}_lib
        
)

if(WITH_GTK)

    add_executable(${PROJECT_NAME}mon-text)

    target_sources(${PROJECT_NAME}mon-text
        PRIVATE
            mon-text.c
            mon.c
    )

    target_link_libraries(${PROJECT_NAME}mon-text
        PRIVATE
            ${PROJECT_NAME}::${PROJECT_NAME}_lib
    )

    add_executable(${PROJECT_NAME}mon-gnome)

    target_sources(${PROJECT_NAME}mon-gnome
        PRIVATE
            mon-gnome.c
            mon.c
            renderer.c
    )

    target_link_libraries(${PROJECT_NAME}mon-gnome
        PRIVATE
            ${PROJECT_NAME}::${PROJECT_NAME}_lib
            $<$<BOOL:${WITH_GTK}>:PkgConfig::GTK3>
    )

    install(TARGETS ${PROJECT_NAME}mon-text ${PROJECT_NAME}mon-gnome)
endif()