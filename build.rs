// Copyright 2024 Martin Pool

use std::env::{self};
use std::fs::{create_dir_all, File};
use std::io::Write;
use std::path::{Path, PathBuf};

// use glob::glob;

static SOURCES: &[&str] = &[
    "src/access.c",
    "src/arg.c",
    "src/argutil.c",
    "src/backoff.c",
    "src/bulk.c",
    "src/cleanup.c",
    "src/climasq.c",
    "src/clinet.c",
    "src/clirpc.c",
    "src/compile.c",
    "src/compress.c",
    "src/cpp.c",
    "src/daemon.c",
    "src/distcc.c",
    "src/dopt.c",
    "src/dotd.c",
    "src/dparent.c",
    "src/dsignal.c",
    "src/emaillog.c",
    "src/exec.c",
    "src/filename.c",
    "src/fix_debug_info.c",
    "src/gcc-id.c",
    // "src/h_argvtostr.c",
    // "src/h_compile.c",
    // "src/h_dotd.c",
    "src/help.c",
    // "src/h_exten.c",
    // "src/h_getline.c",
    // "src/h_hosts.c",
    // "src/h_issource.c",
    "src/history.c",
    "src/hostfile.c",
    "src/hosts.c",
    // "src/h_parsemask.c",
    // "src/h_sa2str.c",
    // "src/h_scanargs.c",
    // "src/h_strip.c",
    "src/implicit.c",
    "src/include_server_if.c",
    "src/io.c",
    "src/loadfile.c",
    "src/lock.c",
    "src/lsdistcc.c",
    // "src/mon.c",
    // "src/mon-gnome.c",
    // "src/mon-notify.c",
    // "src/mon-text.c",
    "src/ncpus.c",
    "src/netutil.c",
    "src/prefork.c",
    "src/pump.c",
    "src/remote.c",
    // "src/renderer.c", // GTK
    "src/rpc.c",
    "src/rslave.c",
    "src/rustglue.c",
    "src/safeguard.c",
    "src/sendfile.c",
    "src/serve.c",
    "src/setuid.c",
    "src/snprintf.c",
    "src/srvnet.c",
    "src/srvrpc.c",
    "src/ssh.c",
    "src/state.c",
    "src/stats.c",
    "src/stringmap.c",
    "src/strip.c",
    "src/tempfile.c",
    "src/timefile.c",
    "src/timeval.c",
    "src/trace.c",
    "src/traceenv.c",
    "src/util.c",
    "src/where.c",
    // "src/zeroconf.c",
    // "src/zeroconf-reg.c",
];

static HAVE: &[&str] = &[
    "HAVE_MKDTEMP",
    "HAVE_IN_PORT_T",
    "HAVE_IN_ADDR_T",
    "HAVE_SETSID",
    "HAVE_STRING_H",
    "HAVE_CTYPE_H",
    "HAVE_STDLIB_H",
    "HAVE_SNPRINTF",
    "HAVE_VSNPRINTF",
    "HAVE_STRERROR",
    "HAVE_C99_VSNPRINTF",
    "HAVE_ASPRINTF",
    "HAVE_SYS_RESOURCE_H",
    "HAVE_STRSEP",
    "HAVE_GETCWD",
    "HAVE_WAIT4",
];

fn main() {
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR should be set by cargo"));
    let config_dir = out_dir.join("include");
    create_dir_all(&config_dir).unwrap();

    // TODO: Unclear what this should be for Rust builds; it may not really be used?
    let prefix = Path::new("/opt/distcc");
    let libdir = prefix.join("lib");

    File::create(config_dir.join("config.h"))
        .expect("create config.h")
        .write_all("#define HAVE_VA_COPY\n".as_bytes())
        .expect("write config.h");
    // TODO: Maybe, to build both the client and server we need a core library
    // and then specific libraries that link in distcc.c and distccd.c?

    // Exclude auth for now, to avoid messing with gssapi.
    // let all_sources = glob("src/*.c")
    //     .expect("glob src/*.c")
    //     .collect::<Result<Vec<PathBuf>, _>>()
    //     .expect("glob src/*.c")
    //     .into_iter()
    //     .filter(|s| {
    //         let name = s.file_name().unwrap().to_string_lossy();
    //         !name.starts_with("auth") && name )
    //     .collect::<Vec<PathBuf>>();
    let triple = quote_var("TARGET");
    let mut build = cc::Build::new();
    build
        .includes(["src", "lzo"])
        .include(&config_dir)
        .define("SYSCONFDIR", quote("/etc").as_str())
        .define("LIBDIR", quote_path(&libdir).as_str())
        .define("RUST_MAIN", None)
        .define("RETSIGTYPE", "void")
        .define(
            "PACKAGE_BUGREPORT",
            quote("https://github.com/distcc/distcc").as_ref(),
        )
        .define("NATIVE_COMPILER_TRIPLE", triple.as_str()) // TODO: Is this "native" in the right sense?
        .define("GNU_HOST", triple.as_str())
        .define("PACKAGE_VERSION", quote_var("CARGO_PKG_VERSION").as_str())
        .files(["lzo/minilzo.c"])
        .files(SOURCES);
    for &h in HAVE {
        build.define(h, None);
    }
    build.compile("distcc-rs");
    let bindings = bindgen::Builder::default()
        .header("src/rustbindings.h")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        .generate()
        .expect("Unable to generate bindings");
    bindings
        .write_to_file(out_dir.join("bindings.rs"))
        .expect("write bindings.rs");
    // println!("cargo:rustc-link-lib=iberty");
    println!("cargo:rerun-if-changed=src");
}

fn quote(s: &str) -> String {
    format!("\"{}\"", s)
}

fn quote_path(p: &Path) -> String {
    format!("\"{}\"", p.to_str().unwrap())
}

fn quote_var(s: &str) -> String {
    if let Ok(v) = env::var(s) {
        quote(&v)
    } else {
        panic!("{} is not set", s)
    }
}
