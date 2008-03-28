AC_INIT(distcc_pump, 
        esyscmd(source version.sh && echo -n $DISTCC_PUMP_VERSION), 
	distcc-pump@google.com)
AC_CHECK_PROGS(PYTHON, [python2.4 python2.3 python-2.3 python2.2 python-2.2 python])
AC_ARG_VAR(PYTHON, [Python interpreter])
AC_CONFIG_AUX_DIR(distcc)
AC_CONFIG_SUBDIRS(include_server distcc)
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([pump], [chmod +x pump])
AC_PROG_INSTALL
AC_OUTPUT
