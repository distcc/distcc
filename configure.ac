dnl Process this file with autoconf to produce a configure script

#		"We are ugly but we have the music"
#			-- Leonard Cohen
#
# http://jerkcity.com/jerkcity1335.html

# This is the configure script for distcc.
# Copyright (C) 2002, 2003, 2004 by Martin Pool

# As of 0.6cvs, distcc no longer uses automake, only autoconf.
AC_PREREQ(2.69)
AC_INIT(distcc, 3.4, distcc@lists.samba.org)

AC_CONFIG_HEADERS(src/config.h)
AC_CONFIG_MACRO_DIRS([m4])

AC_CANONICAL_HOST
AC_MSG_CHECKING([if the host system type is Linux])
case "${host_os}" in
linux*)
	AC_MSG_RESULT(yes)
	AC_DEFINE(HAVE_LINUX, 1, [Define to 1 if the host system type is Linux.])
	;;
*)
	AC_MSG_RESULT(no)
	;;
esac

# FreeBSD installs its version of libpopt into /usr/local/, but does
# not put that on the default library and header path.
# Solaris doesn't even ship libpopt.  We used to add that path if
# building on *bsd*, but bje points out that will break
# cross-compilation, and it's kind of ugly anyhow.  Anyhow, you can
# either set CPPFLAGS and LDFLAGS when running configure, or use
# --with-extra-foo.

# Check for socklen_t, and do something appropriate on systems
# that don't have it.
AC_CHECK_TYPE([socklen_t], ,[
	AC_MSG_CHECKING([for socklen_t equivalent])
	AC_CACHE_VAL([dcc_cv_socklen_t_equiv],
	[
	# Systems have either "struct sockaddr{,_in} *" or
	# "void *" as the second argument to getpeername
	dcc_cv_socklen_t_equiv=
	for arg2 in "struct sockaddr_in" "struct sockaddr" void; do
		for t in int size_t unsigned long "unsigned long"; do
			AC_TRY_COMPILE([
#include <sys/types.h>
#include <sys/socket.h>

int getpeername (int, $arg2 *, $t *);
],[
$t len;
getpeername(0,0,&len);
],[
	dcc_cv_socklen_t_equiv="$t"
	break
])
		done
	done

	if test "x$dcc_cv_socklen_t_equiv" = x; then
		AC_MSG_ERROR([Cannot find a type to use in place of socklen_t])
	fi
	])
	AC_MSG_RESULT($dcc_cv_socklen_t_equiv)
	AC_DEFINE_UNQUOTED(socklen_t, $dcc_cv_socklen_t_equiv,
		[type to use in place of socklen_t if not defined])],
	[#include <sys/types.h>
#include <sys/socket.h>])

dnl Android requires -llog for printf and friends
dnl and will only run position independent executables
AC_MSG_CHECKING([if building on Android])
AC_TRY_COMPILE([], [
  #ifdef __BIONIC__
  int ok;
  (void)ok;
  #else
  choke me
  #endif],
[AC_MSG_RESULT(yes)
 LDFLAGS="$LDFLAGS -llog -pie"],
[AC_MSG_RESULT(no)])

# TODO: Handle program transform rules by autoconf.

### Checks for configure options

AC_ARG_WITH(included-popt,
	AC_HELP_STRING([--with-included-popt], [use bundled popt library, not from system]))

AC_ARG_ENABLE(pump-mode,
	AC_HELP_STRING([--disable-pump-mode],
		[include server support (pump mode), requires python]),
		[pump_mode=${enableval}], [pump_mode=yes])

# We prefer to use the latest Python
if test "x${pump_mode}" = xyes; then
	AM_PATH_PYTHON([3.1],,[:])
	AC_ARG_VAR(PYTHON, [Python interpreter])
	AC_SUBST(PYTHON_RELATIVE_LIB)
fi

AC_ARG_ENABLE(rfc2553,
	AC_HELP_STRING([--enable-rfc2553], [use getaddrinfo, getnameinfo, etc]))

if test x"$enable_rfc2553" = xyes
then
    AC_DEFINE(ENABLE_RFC2553,1,[use getaddrinfo(), getnameinfo(), etc])
fi

AC_ARG_WITH(gnome,
        AC_HELP_STRING([--with-gnome], [build GNOME-based monitor]))

if test x"$with_gnome" = xyes
then
    GNOME_BIN=distccmon-gnome
    AC_DEFINE(WITH_GNOME,1,[Use GNOME])
fi

AC_ARG_WITH(gtk,
        AC_HELP_STRING([--with-gtk], [build GTK+-based monitor]))
if test x"$with_gtk" = xyes
then
    GNOME_BIN=distccmon-gnome
    AC_DEFINE(WITH_GTK,1,[Use GTK+])
fi

AC_ARG_ENABLE(profile,
        AC_HELP_STRING([--enable-profile], [turn on gprof]))

if test x"$enable_profile" = xyes
then
    CFLAGS="$CFLAGS -pg -g"
fi

AC_ARG_ENABLE(Werror,
        AC_HELP_STRING([--disable-Werror], [don't use gcc's -Werror option when building]))

# Now get the package configuration information for whatever packages
# we need.  It's faster to just do it once during configuration.
if test "x${with_gnome}" = xyes
then
    GNOME_PACKAGES="gtk+-3.0 libgnome-3.0 libgnomeui-3.0 pango"
elif test "x${with_gtk}" = xyes
then
    GNOME_PACKAGES="gtk+-3.0"
else
    GNOME_PACKAGES=""
fi

for pkg in $GNOME_PACKAGES
do
    AC_MSG_CHECKING([version of $pkg])
    if gnomevers=`pkg-config --modversion $pkg`
    then
        AC_MSG_RESULT($gnomevers)
    else
        AC_MSG_ERROR([$pkg was not found by pkg-config])
    fi
done

if test x${with_gnome} = xyes -o x${with_gtk} = xyes
then
    INSTALL_GNOME="install-gnome-data"
    UNINSTALL_GNOME="uninstall-gnome-data"
    AC_MSG_CHECKING([GNOME/GTK+ cflags])
    GNOME_CFLAGS="`pkg-config --cflags $GNOME_PACKAGES`" || AC_MSG_ERROR([failed to get cflags])
    AC_MSG_RESULT([${GNOME_CFLAGS}])

    AC_MSG_CHECKING([GNOME/GTK+ libraries])
    GNOME_LIBS="`pkg-config --libs $GNOME_PACKAGES`" || AC_MSG_ERROR([failed to get libs])
    AC_MSG_RESULT([${GNOME_LIBS}])
fi

AC_SUBST(GNOME_PACKAGES)
AC_SUBST(GNOME_CFLAGS)
AC_SUBST(GNOME_LIBS)
AC_SUBST(INSTALL_GNOME)
AC_SUBST(UNINSTALL_GNOME)

dnl Checks for programs
AC_PROG_CC
WERROR_CFLAGS=""
POPT_CFLAGS=""
PYTHON_CFLAGS=""
if test x"$GCC" = xyes
then
    CFLAGS="$CFLAGS -MD \
-W -Wall -Wimplicit \
-Wshadow -Wpointer-arith -Wcast-align -Wwrite-strings \
-Waggregate-return -Wstrict-prototypes -Wmissing-prototypes \
-Wnested-externs -Wmissing-declarations"

    # -Wuninitialized requires -O
    case "$CFLAGS" in "-O"*|*" -O"*)
        CFLAGS="$CFLAGS -Wuninitialized"
        ;;
    esac
    # Would like -Wunreachable-code here, but it generates too many false
    # positives.

    # We want warnings to be treated as errors,
    # unless the --disable-Werror configure option was used.
    # Note that we can't include this in CFLAGS,
    # because that would affect the configure tests,
    # causing some of the tests to fail when they should succeed.
    if test x"$enable_Werror" != xno
    then
      WERROR_CFLAGS="-Werror"
	  # -Wp,-U_FORTIFY_SOURCE is to turn off _FORTIFY_SOURCE on systems where
	  # it's in the Python Makefile (and hence inherited by us).
	  # -Werror -D_FORTIFY_SOURCE gives compiler errors for some distcc routines that
	  # ignore the return value from libc functions (like getcwd).
	  # That would cause this code to not compile, which is no good.
	  PYTHON_CFLAGS="-Wp,-U_FORTIFY_SOURCE"
    fi

    # Additional flags for compiling Python extension modules.
    # We disable -Wmissing-prototypes and -Wmissing-declarations,
    # which don't apply to python extensions (it exports global fns via a
    # pointer), and -Wwrite-strings, which just had too many false
    # positives (for Python 2.2, anyway; looks like these may be fixed
    # in Python 2.5).
    PYTHON_CFLAGS="$PYTHON_CFLAGS -Wno-missing-prototypes -Wno-missing-declarations \
-Wno-write-strings"

    # For popt/*.c, we disable unused variable warnings.
    # When using Apple GCC/Clang you have to explicitly disable unused parameters.
    POPT_CFLAGS="-Wno-unused -Wno-unused-parameter"

    AC_MSG_NOTICE([Adding gcc options: $CFLAGS])
fi
AC_SUBST(WERROR_CFLAGS)
AC_SUBST(POPT_CFLAGS)
AC_SUBST(PYTHON_CFLAGS)
AC_ISC_POSIX


# Apple's OS X gcc has a cpp-precomp "feature" that breaks standard
# variadic macros.  If we can disable it, do so.


# This is needed for our included version of lzo.
CPPFLAGS="$CPPFLAGS -DMINILZO_HAVE_CONFIG_H"

# This is needed for our included version of popt.
# TODO(csilvers): figure out what that above comment means, and if it's true.
CPPFLAGS="$CPPFLAGS -DHAVE_CONFIG_H"

# Needed to get various GNU-only functions.  They're all autodetected
# by configure so we can cope without them, but we want them if they're
# there.
CPPFLAGS="$CPPFLAGS -D_GNU_SOURCE"

AC_PROG_MAKE_SET
AC_PROG_INSTALL

AC_C_INLINE
AC_C_BIGENDIAN

########################################################################
### Checks for header files

# Some of these are needed by popt (or other libraries included in the future).

AC_CHECK_HEADERS([unistd.h sys/types.h sys/sendfile.h])
AC_CHECK_HEADERS([ctype.h sys/resource.h sys/socket.h sys/select.h])
AC_CHECK_HEADERS([netinet/in.h], [], [],
[#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
])
AC_CHECK_HEADERS([arpa/nameser.h], [], [],
[#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
])
AC_CHECK_HEADERS([resolv.h], [], [],
[#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif
])

AC_CHECK_HEADERS([float.h mcheck.h alloca.h sys/mman.h sys/loadavg.h])
AC_CHECK_HEADERS([elf.h])
AC_CHECK_HEADERS([fnmatch.h])

######################################################################
dnl Checks for types

AC_CHECK_TYPES([in_port_t, in_addr_t], , ,
	       [
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif
#if HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif
])



########################################################################
### Checks for libraries.

# The following test taken from the cvs sources via Samba:
# If we can't find connect, try looking in -lsocket, -lnsl, and -linet.
# The Irix 5 libc.so has connect and gethostbyname, but Irix 5 also has
# libsocket.so which has a bad implementation of gethostbyname (it
# only looks in /etc/hosts), so we only look for -lsocket if we need
# it.
AC_SEARCH_LIBS(gethostent, [nsl])
AC_SEARCH_LIBS(setsockopt, [socket])
AC_SEARCH_LIBS(hstrerror, [resolv])
AC_SEARCH_LIBS(inet_aton, [resolv])

if test x"$with_included_popt" != x"yes"  && test x"$with_included_popt" != xno
then
    # If not explicitly requested, guess.
    # People might have the library but not the header, in which case we
    # still need to use the included copy.
    AC_CHECK_LIB(popt, poptGetContext, , [with_included_popt=yes])
    AC_CHECK_HEADER(popt.h, , [with_included_popt=yes])
fi
AC_CACHE_SAVE

AC_MSG_CHECKING([whether to use included libpopt])
if test x"$with_included_popt" = x"yes"
then
    AC_MSG_RESULT($srcdir/popt)
    # popt_OBJS gets appended to distccd object list
    BUILD_POPT='$(popt_OBJS)'
    POPT_INCLUDES='-I"$(srcdir)/popt"'
else
    AC_MSG_RESULT(no)
    POPT_INCLUDES=''
    AC_CHECK_LIB(popt, poptGetContext, ,
                 AC_MSG_ERROR([Cannot find system libpopt]))
fi

AC_ARG_WITH(libiberty,
        AC_HELP_STRING([--without-libiberty], [build without libiberty (no @file support)]))

if test x"$with_libiberty" != xno; then
    AC_CHECK_LIB([iberty], [expandargv],
                 ,
                 AC_MSG_ERROR([Cannot find libiberty]))
    AC_CHECK_HEADERS([libiberty.h libiberty/libiberty.h],
                     [break])
    if (test "$ac_cv_header_libiberty_h" != yes) &&
       (test "$ac_cv_header_libiberty_libiberty_h" != yes); then
        AC_MSG_ERROR([Cannot find libiberty.h])
    fi
fi

########################################################################
# Check for types
AC_TYPE_SIGNAL


########################################################################
# Checks for library functions, using libraries discovered above

# The line below was commented out: it should not be necessary for the
# AC_CHECK_FUNCS, which link against libraries on the host. For the subsequent
# explicit compilation tests below, no distcc libraries are used.
# TODO(klarlund): remove the line.

# CPPFLAGS="$CPPFLAGS -I$srcdir/src"

AC_CHECK_FUNCS([getpagesize])
AC_CHECK_FUNCS([sendfile setsid flock lockf hstrerror strerror setuid setreuid])
AC_CHECK_FUNCS([getuid geteuid mcheck wait4 wait3 waitpid setgroups])
AC_CHECK_FUNCS([snprintf vsnprintf vasprintf asprintf getcwd getwd mkdtemp])
AC_CHECK_FUNCS([getrusage strsignal gettimeofday])
AC_CHECK_FUNCS([getaddrinfo getnameinfo inet_ntop inet_ntoa])
AC_CHECK_FUNCS([strndup strsep mmap strlcpy])

AC_CHECK_FUNCS([getloadavg])
AC_CHECK_FUNCS([getline])

AC_CHECK_FUNCS([fstatat])

AC_CHECK_DECLS([snprintf, vsnprintf, vasprintf, asprintf, strndup])

AC_MSG_CHECKING([if mmap() supports MAP_FAILED])
AC_TRY_COMPILE([
#include <stddef.h>
#ifdef HAVE_SYS_MMAN_H
#  include <sys/mman.h>
#endif],[
#ifdef HAVE_MMAP
if (mmap (NULL, 0, 0, 0, 0, 0) == MAP_FAILED)
	return 0;
#else
#error mmap unavailable
#endif], AC_MSG_RESULT(yes), [
	AC_MSG_RESULT(no)
	AC_DEFINE([MAP_FAILED], [(void *)-1L],
	[Define if MAP_FAILED constant not available])
])

dnl
dnl Test if the preprocessor understand vararg macros
dnl
AC_MSG_CHECKING([for vararg macro support])
AC_TRY_COMPILE([#define func(a, b...) do { } while (0)],
[func("a"); func("a", "b"); func("a", "b", "c")],
[AC_MSG_RESULT(yes)
 AC_DEFINE(HAVE_VARARG_MACROS, , [Define if your cpp has vararg macros])],
[AC_MSG_RESULT(no)])

AC_CACHE_CHECK([for va_copy],dcc_cv_HAVE_VA_COPY,[
AC_TRY_LINK([#include <stdarg.h>
va_list ap1,ap2;], [va_copy(ap1,ap2);],
dcc_cv_HAVE_VA_COPY=yes,dcc_cv_HAVE_VA_COPY=no)])
if test x"$dcc_cv_HAVE_VA_COPY" = x"yes"; then
    AC_DEFINE(HAVE_VA_COPY,1,[Whether va_copy() is available])
fi
AC_CACHE_CHECK([for __va_copy],dcc_cv_HAVE_UNDERSCORE_UNDERSCORE_VA_COPY,[
AC_TRY_LINK([#include <stdarg.h>
va_list ap1,ap2;], [__va_copy(ap1,ap2);],
dcc_cv_HAVE_UNDERSCORE_UNDERSCORE_VA_COPY=yes,dcc_cv_HAVE_UNDERSCORE_UNDERSCORE_VA_COPY=no)])
if test x"$dcc_cv_HAVE_UNDERSCORE_UNDERSCORE_VA_COPY" = x"yes"; then
    AC_DEFINE(HAVE_UNDERSCORE_UNDERSCORE_VA_COPY,1,[Whether __va_copy() is available])
fi

AC_CACHE_CHECK([for C99 vsnprintf],rsync_cv_HAVE_C99_VSNPRINTF,[
AC_TRY_RUN([
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdarg.h>
static int foo(const char *format, ...) {
       va_list ap;
       int len;
       char buf[5];

       va_start(ap, format);
       len = vsnprintf(0, 0, format, ap);
       va_end(ap);
       if (len != 5) return 1;

       if (snprintf(buf, 3, "hello") != 5 || strcmp(buf, "he") != 0) return 1;

       return 0;
}
int main() { return foo("hello"); }
],
rsync_cv_HAVE_C99_VSNPRINTF=yes,rsync_cv_HAVE_C99_VSNPRINTF=no,rsync_cv_HAVE_C99_VSNPRINTF=cross)])
if test x"$rsync_cv_HAVE_C99_VSNPRINTF" = x"yes"; then
    AC_DEFINE(HAVE_C99_VSNPRINTF, 1, [define if vsnprintf is C99 compliant])
fi


AC_CACHE_CHECK([for working socketpair],rsync_cv_HAVE_SOCKETPAIR,[
AC_TRY_RUN([
#include <sys/types.h>
#include <sys/socket.h>

int main() {
       int fd[2];
       return (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != -1) ? 0 : 1;
}],
rsync_cv_HAVE_SOCKETPAIR=yes,rsync_cv_HAVE_SOCKETPAIR=no,rsync_cv_HAVE_SOCKETPAIR=cross)])
if test x"$rsync_cv_HAVE_SOCKETPAIR" = x"yes"; then
    AC_DEFINE(HAVE_SOCKETPAIR, 1, [define if you have a working socketpair])
fi

dnl Checks for structures
AC_CHECK_MEMBER([struct sockaddr_storage.ss_family],
    AC_DEFINE(HAVE_SOCKADDR_STORAGE, 1, [define if you have struct sockaddr_storage]),,
    [#include <sys/socket.h>])

AC_ARG_WITH(avahi,
        AC_HELP_STRING([--without-avahi], [build without avahi]))

dnl check for avahi
if test x"$with_avahi" != xno; then
    PKG_CHECK_MODULES(AVAHI, [avahi-client >= 0.6],
    [AC_DEFINE(HAVE_AVAHI, 1, [defined if Avahi is available])
    CFLAGS="$CFLAGS $AVAHI_CFLAGS"
    LIBS="$LIBS $AVAHI_LIBS"
    ZEROCONF_COMMON_OBJS="src/zeroconf.o src/gcc-id.o"
    ZEROCONF_DISTCC_OBJS=""
    ZEROCONF_DISTCCD_OBJS="src/zeroconf-reg.o"],
    [ZEROCONF_COMMON_OBJS=""
    ZEROCONF_DISTCC_OBJS=""
    ZEROCONF_DISTCCD_OBJS=""])
    AC_SUBST(ZEROCONF_COMMON_OBJS)
    AC_SUBST(ZEROCONF_DISTCC_OBJS)
    AC_SUBST(ZEROCONF_DISTCCD_OBJS)
fi

AUTH_COMMON_OBJS=""
AUTH_DISTCC_OBJS=""
AUTH_DISTCCD_OBJS=""

#check for GSS-API
AC_ARG_WITH([auth],
	    [AS_HELP_STRING([--with-auth],
	    [provide mutual authentication services via the GSS-API])])

if test x"$with_auth" = xyes; then
        AC_SEARCH_LIBS([gss_init_sec_context],
                        [gssapi gssapi_krb5 gss],
	                AC_DEFINE(HAVE_GSSAPI, 1, [Define if the GSS_API is available])
	                AUTH_COMMON_OBJS="src/auth_common.o"
	                AUTH_DISTCC_OBJS="src/auth_distcc.o"
	                AUTH_DISTCCD_OBJS="src/auth_distccd.o",
	                AC_MSG_FAILURE([--with-auth was given but no GSS-API library found])
	                AUTH_COMMON_OBJS=""
                        AUTH_DISTCC_OBJS=""
                        AUTH_DISTCCD_OBJS="")
fi

AC_SUBST(AUTH_COMMON_OBJS)
AC_SUBST(AUTH_DISTCC_OBJS)
AC_SUBST(AUTH_DISTCCD_OBJS)

AX_PTHREAD
LIBS="$PTHREAD_LIBS $LIBS"
CFLAGS="$CFLAGS $PTHREAD_CFLAGS"
CC="$PTHREAD_CC"

dnl ##### Output
AC_SUBST(CFLAGS)
AC_SUBST(LDFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(POPT_INCLUDES)
AC_SUBST(BUILD_POPT)
AC_SUBST(GNOME_BIN)


# Sometimes canonical triples as used by configure differ from GCC ones
# x86: configure: x86_64-pc-linux-gnu, GCC: x86_64-linux-gnu
# ALT Linux: configure: ${arch}-alt-linux-gnu, GCC: ${arch}-alt-linux
# Therefore ask the compiler for its triple
if test "x${GCC}" = xyes ; then
	native_compiler_triple=`$CC -dumpmachine`
fi
if test "x$native_compiler_triple" = "x"; then
	native_compiler_triple="$host"
fi
AC_MSG_NOTICE([Native compiler triple: $native_compiler_triple])

AC_DEFINE_UNQUOTED(NATIVE_COMPILER_TRIPLE, ["$native_compiler_triple"], [Native compiler triple])

AC_DEFINE_UNQUOTED(GNU_HOST, ["$host"], [Your gnu-style host triple])
# The '.stamp-conf' files force creation of the containing directories in the
# build tree.
AC_CONFIG_FILES([Makefile] popt/.stamp-conf lzo/.stamp-conf)
AC_CONFIG_FILES([pump], [chmod +x pump])
AC_OUTPUT


##### Finalization

# Display success, installation paths, and GPL licence statement.
echo ' '
${MAKE-make} showpaths
echo ' '
cat <<EOF

    $PACKAGE_NAME $PACKAGE_VERSION configured

    Copyright (C) 2002, 2003, 2004 by Martin Pool <mbp@samba.org>
    Portions Copyright 2005 Google Inc.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
    USA.

After installing distcc, please complete the survey in the file "survey.txt".

EOF
