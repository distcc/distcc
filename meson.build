project('distcc', 'c',
        version : '3.2rc1',
        license : 'GPLv2+',
        default_options: [
                'c_std=gnu99',
                'prefix=/usr/local',
                'sysconfdir=/etc',
        ],
        meson_version : '>= 0.44',
       )

conf = configuration_data()
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('PACKAGE_BUGREPORT', 'distcc@lists.samba.org')
############################################################
cc = meson.get_compiler('c')
# If you use any of these functions you need to write alternate implementations
# in util.c and add it below under functions to get a HAVE_x macro
add_global_arguments('-D_GNU_SOURCE', language : 'c')

foreach arg : ['-W',
               '-Wall',
               '-Wextra',
               '-Wimplicit',
               '-Wshadow',
               '-Wpointer-arith',
               '-Wcast-align',
               '-Wwrite-strings',
               '-Waggregate-return',
               '-Wstrict-prototypes',
               '-Wmissing-prototypes',
               '-Wnested-externs',
               '-Wmissing-declarations',
              ]
    if cc.has_argument(arg)
        add_project_arguments(arg, language : 'c')
    endif
endforeach

foreach arg : ['unused-parameter',
              ]
        if cc.has_argument('-W' + arg)
                add_project_arguments('-Wno-' + arg, language : 'c')
        endif
endforeach

foreach header : ['unistd.h',
                  'sys/types.h',
                  'sys/sendfile.h',
                  'ctype.h',
                  'sys/resource.h',
                  'sys/socket.h',
                  'sys/select.h',
                  'sys/socket.h',
                  'netinet/in.h',
                  'arpa/nameser.h',
                  'resolv.h',
                  'float.h',
                  'mcheck.h',
                  'alloca.h',
                  'sys/mman.h',
                  'sys/loadavg.h',
                  'elf.h',
                  'fnmatch.h',
                 ]
    if cc.has_header(header)
        conf.set('HAVE_' + header.underscorify().to_upper(), 1)
    endif
endforeach
foreach type: ['in_port_t',
               'in_addr_t',
              ]

    conf.set('HAVE_' + type.underscorify().to_upper(),
       cc.has_type(type, prefix : '''
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/nameser.h>
'''))
endforeach
foreach function: ['getpagesize',
                   'sendfile',
                   'setsid',
                   'flock',
                   'lockf',
                   'hstrerror',
                   'strerror',
                   'setuid',
                   'setreuid',
                   'getuid',
                   'geteuid',
                   'mcheck',
                   'wait4',
                   'wait3',
                   'waitpid',
                   'setgroups',
                   'snprintf',
                   'vsnprintf',
                   'vasprintf',
                   'asprintf',
                   'getcwd',
                   'getwd',
                   'mkdtemp',
                   'getrusage',
                   'strsignal',
                   'gettimeofday',
                   'getaddrinfo',
                   'getnameinfo',
                   'inet_ntop',
                   'inet_ntoa',
                   'strndup',
                   'strsep',
                   'strlcpy',
                   'getloadavg',
                   'getline',
                   'snprintf',
                   'vsnprintf',
                   'vasprintf',
                   'asprintf',
                  ]
    if cc.has_function(function)
        conf.set('HAVE_' + function.underscorify().to_upper(), 1)
    endif
endforeach
############################################################

prefixdir = get_option('prefix')
if not prefixdir.startswith('/')
    error('Prefix is not absolute: "@0@"'.format(prefixdir))
endif

docdir = join_paths(prefixdir, get_option('docdir'))

############################################################
# Check for socklen_t, and do something appropiate on systems
# that don't have it.
socklen_t_test_format = '''
#include <sys/types.h>
#include <sys/socket.h>

int getpeername (int, struct sockaddr *, @0@ *);
'''
socklen_t_test = socklen_t_test_format.format('socklen_t')
if cc.compiles(socklen_t_test)
    socklen_t_type = 'socklen_t'
else
    socklen_t_test = socklen_t_test_format.format('size_t')
    if cc.compiles(socklen_t_test)
        socklen_t_type = 'size_t'
    else
        socklen_t_test = socklen_t_test_format.format('unsigned')
        if cc.compiles(socklen_t_test)
            socklen_t_type = 'unsigned'
        else
            error('unable to determine socklen_t type')
        endif
    endif
endif
message('socklen_t type is @0@'.format(socklen_t_type))
if socklen_t_type != 'socklen_t'
    conf.set('socklen_t', socklen_t_type,
         description : 'type to use in place of socklen_t if not defined')
endif
############################################################
# Check if mmap() supports MAP_FAILED
mmap_failed = cc.compiles('''
#include <sys/mman.h>
main() {
if (mmap(0, 0, 0, 0, 0, 0) == MAP_FAILED)
	return 0;
}''', name: 'has MAP_FAILED')
if not mmap_failed
    conf.set('MAP_FAILED', '(void *)-1L',
         description : 'Define if MAP_FAILED constant not available')
endif
############################################################
#TODO this doesn't work will cross compiling
# https://github.com/mesonbuild/meson/issues/2774
if cc.get_id() == 'clang'
    gnu_host = run_command('clang', '-dumpmachine').stdout().strip()
elif cc.get_id() == 'gcc'
    gnu_host = run_command('gcc', '-dumpmachine').stdout().strip()
else
    gnu_host = 'unknown'
endif
gnu_host_format = '"@0@"'
conf.set('GNU_HOST', gnu_host_format.format(gnu_host),
     description : 'Define to the GNU_HOST triplet that distccd will be running on')
############################################################
#Assume basic c99 support
#TODO rip this out of the code
conf.set10('HAVE_VARARG_MACROS', 1, description : 'Define if your cpp has vararg macros')
conf.set10('HAVE_VA_COPY', 1, description : 'Whether va_copy() is available')
conf.set10('HAVE_C99_VSNPRINTF', 1, description: 'define if vsnprintf is C99 compliant')
############################################################
have_working_socketpair = cc.compiles('''
#include <sys/types.h>xt
#include <sys/socket.h>

main() {
       int fd[2];
       exit((socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != -1) ? 0 : 1);
}''')
conf.set10('HAVE_SOCKETPAIR', have_working_socketpair,
    description: 'define if you have a working socketpair')
############################################################
have_sockaddr_storage = cc.has_member('struct sockaddr_storage', 'ss_family', prefix: '#include <sys/socket.h>')
conf.set10('HAVE_SOCKADDR_STORAGE', have_sockaddr_storage,
    description: 'define if you have struct sockaddr_storage')
############################################################
check_retsignal = cc.compiles('''
#include <sys/types.h>
#include <signal.h>

int
main ()
{
return *(signal (0, 0)) (0) == 1;
  ;
  return 0;
}''', name: 'return value of signal handler (NO is OK)')
if check_retsignal
    conf.set('RETSIGTYPE', 'int')
    message('socklen_t type is @0@'.format(socklen_t_type))

    message('signals return int')
else
    conf.set('RETSIGTYPE', 'void')
    message('signals return void')
endif
############################################################
add_global_arguments('-DMINILZO_HAVE_CONFIG_H', language : 'c')

# Needed by popt/system.h:1
add_global_arguments('-DHAVE_CONFIG_H', language : 'c')
popt = dependency('popt', required : false)
conf.set('HAVE_POPT', popt.found(),
        description: 'use system popt')

want_gtk = get_option('gtk')
if want_gtk != 'false'
    gtk = dependency('gtk+-2.0', required : want_gtk == 'true')
else
    gtk = disabler()
endif
conf.set('WITH_GTK', gtk.found(), description: 'use GTK+')

want_auth = get_option('auth')
if want_auth != 'false'
    gssapi = dependency('krb5-gssapi', required : want_auth == 'true')
else
    gssapi = disabler()
endif
conf.set('HAVE_GSSAPI', gssapi.found(), description: 'use GSSAPI')

want_zeroconf = get_option('zeroconf')
if want_zeroconf != 'false'
    avahi = dependency('avahi', required: want_zeroconf == 'true')
else
    avahi = disabler()
endif
conf.set('HAVE_AVAHI', avahi.found(), description : 'use Avahi for Zeroconf/mDNS')

zstd = dependency('libzstd',
    version : '>=1.0',
    fallback : ['zstd', 'zstd_dep'],
    default_options: ['legacy_support=false'],)
conf.set('HAVE_ZSTD', zstd.found(), description : 'use Zstandard for compression')

want_rfc2553 = get_option('rfc2553')
conf.set10('ENABLE_RFC2553', want_rfc2553,
         description : 'use getaddrinfo(), getnameinfo(), etc')

format_c_string = '"@0@"'
sysconfdir = get_option('sysconfdir')
conf.set('SYSCONFDIR', format_c_string.format(sysconfdir))

#used by src/mon-gnome.c
pixmapsdir =  join_paths(get_option('datadir'), 'pixmaps')
conf.set('PKGDATADIR', format_c_string.format(join_paths(prefixdir, pixmapsdir)))

configure_file(
  output: 'config.h',
  configuration: conf
)
############################################################


includes = include_directories('src',
                               'lzo',
                               'popt',
                              )

lzo_sources = ['lzo/minilzo.c',
       'lzo/minilzo.h',
       'lzo/lzoconf.h',
       'lzo/lzodefs.h',
      ]

common_sources = ['src/arg.c', 'src/argutil.c',
    'src/cleanup.c',
    'src/compress-lzox1.c', 'src/compress-zstd.c',
    'src/trace.c', 'src/util.c', 'src/io.c', 'src/exec.c',
    'src/rpc.c', 'src/tempfile.c', 'src/bulk.c', 'src/help.c', 'src/filename.c',
    'src/lock.c',
    'src/netutil.c',
    'src/pump.c',
    'src/sendfile.c',
    'src/safeguard.c', 'src/snprintf.c', 'src/timeval.c',
    'src/dotd.c','src/traceenv.c',
    'src/hosts.c', 'src/hostfile.c', 'src/state.c',
    'src/implicit.c', 'src/loadfile.c',
    'src/clirpc.c', 'src/clinet.c', 'src/include_server_if.c', 'src/emaillog.c',
    'src/srvrpc.c',
    ]

#distcc
common_sources += ['src/backoff.c',
    'src/climasq.c',
    'src/compile.c', 'src/cpp.c',
    'src/remote.c',
    'src/ssh.c', 'src/strip.c',
    'src/timefile.c',
    'src/where.c',
    ]

#distccd
common_sources += ['src/access.c',
    'src/dopt.c', 'src/dparent.c', 'src/dsignal.c',
    'src/ncpus.c',
    'src/prefork.c',
    'src/stringmap.c',
    'src/serve.c', 'src/setuid.c', 'src/srvnet.c', 'src/state.c',
    'src/stats.c',
    'src/fix_debug_info.c',]

if not popt.found()
    common_sources += [
        'popt/findme.c',
        'popt/popt.c',
        'popt/poptconfig.c',
        'popt/popthelp.c',
        'popt/poptparse.c',
    ]
endif

if gssapi.found()
    common_sources += ['src/auth_common.c']
    common_sources += ['src/auth_distcc.c']
    common_sources += ['src/auth_distccd.c']
endif

if avahi.found()
    common_sources += ['src/zeroconf.c', 'src/gcc-id.c']
    common_sources += ['src/zeroconf-reg.c']
endif

lsdistcc_sources = [
    'src/lsdistcc.c',
    'src/rslave.c'
    ]

distccmon_common_sources = [
    'src/mon.c',
    ]

distccmon_text_sources = distccmon_common_sources + ['src/mon-text.c']

if gtk.found()
    distccmon_gnome_sources = distccmon_common_sources + [
        'src/mon-gnome.c',
        'src/renderer.c',
        'src/history.c',
    ]
endif

minilzo = static_library('minilzo', lzo_sources)

distcc_common = static_library('distcc-common', common_sources,
    include_directories: includes,
    dependencies: [
        zstd,
    ])

executable('distcc', sources : 'src/distcc.c',
    include_directories: includes,
    link_with: [
        minilzo,
        distcc_common,
        ],
    dependencies: [
        gssapi,
        avahi,
        zstd,
        ],
    install : true)
executable('distccd', sources : 'src/daemon.c',
    include_directories: includes,
    link_with: [minilzo,
                distcc_common,
                ],
    dependencies: [
        popt,
        gssapi,
        avahi,
        zstd,
        ],
    install : true)
executable('lsdistcc', sources: lsdistcc_sources,
    include_directories: includes,
    link_with: [minilzo,
                distcc_common],
    dependencies: [
        ],
    install: true)
executable('distccmon-text', sources : distccmon_text_sources,
    include_directories: includes,
    link_with: [distcc_common],
    dependencies: [
        popt,
        ],
    install: true)
if gtk.found()
    executable('distccmon-gnome', sources : distccmon_gnome_sources,
        include_directories: includes,
        link_with: [distcc_common],
        dependencies: [
            popt,
            gtk,
            ],
        install: true)
endif

pump_conf = configuration_data()
py3_mod = import('python3')
pump_conf.set('PYTHON', py3_mod.find_python().path())
pump_conf.set('prefix', get_option('prefix'))
pump_conf.set('sysconfdir', get_option('sysconfdir'))
srcdir = 'lib/distcc-pump'
pythonsrcdir = join_paths(srcdir, 'include_server')
pump_conf.set('srcdir', join_paths(get_option('prefix'), pythonsrcdir))
pump_conf.set('include_server', join_paths(get_option('prefix'), pythonsrcdir, 'include_server.py'))
configure_file(
    input: 'pump.in',
    output: 'pump',
    configuration: pump_conf,
    install_dir: join_paths(get_option('prefix'), 'bin')
)
py3_dep = dependency('python3', required : true)
include_server = py3_mod.extension_module('distcc_pump_c_extensions',
    'include_server/c_extensions/distcc_pump_c_extensions_module.c',
    include_directories: includes,
    link_with: [distcc_common, minilzo],
    dependencies : [py3_dep],
    install: true,
    install_dir: join_paths(get_option('prefix'), pythonsrcdir )
)
######################################################
manpages = [
    'man/distccmon-text.1',
    'man/distcc.1',
    'man/lsdistcc.1',
    'man/include_server.1',
    'man/pump.1',
    'man/distccd.1',
]
install_man(manpages)
install_data('gnome/distccmon-gnome-icon.png', install_dir: pixmapsdir)

install_data([
    'include_server/basics.py',
    'include_server/cache_basics.py',
    'include_server/compiler_defaults.py',
    'include_server/compress_files.py',
    'include_server/include_analyzer.py',
    'include_server/include_analyzer_memoizing_node.py',
    'include_server/include_server.py',
    'include_server/macro_eval.py',
    'include_server/mirror_path.py',
    'include_server/parse_command.py',
    'include_server/parse_file.py',
    'include_server/run.py',
    'include_server/setup.py',
    'include_server/statistics.py',
    'include_server/__init__.py',
], install_dir: join_paths(get_option('prefix'), pythonsrcdir))
######################################################
executable('h_argvtostr', 'src/h_argvtostr.c', link_with: [distcc_common] )
executable('h_compile', 'src/h_compile.c', link_with: [distcc_common,minilzo,],
    dependencies: gssapi)
executable('h_dotd', 'src/h_dotd.c', link_with: [distcc_common] )
executable('h_exten', 'src/h_exten.c', link_with: [distcc_common] )
executable('h_fix_debug_info', 'src/fix_debug_info.c', link_with: [distcc_common],
    c_args : '-DTEST',)
executable('h_getline', 'src/h_getline.c', link_with: [distcc_common] )
executable('h_issource', 'src/h_issource.c', link_with: [distcc_common] )
executable('h_parsemask', 'src/h_parsemask.c', link_with: [distcc_common])
executable('h_sa2str', 'src/h_sa2str.c', link_with: [distcc_common])
executable('h_scanargs', 'src/h_scanargs.c', link_with: [distcc_common])
executable('h_strip', 'src/h_strip.c', link_with: [distcc_common])
executable('h_hosts', 'src/h_hosts.c', link_with: [distcc_common])

test('distcc without pump', py3_mod.find_python(), args: ['../test/testdistcc.py'],
    env: 'PATH=' + meson.build_root() + ':/usr/bin:/bin', timeout: 360, is_parallel: false)
test('distcc with pump', py3_mod.find_python(), args: ['../test/testdistcc.py', '--pump'],
    env: 'PATH=' + meson.build_root() + ':/usr/bin:/bin', timeout: 360, is_parallel: false)
