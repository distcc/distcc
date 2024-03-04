// Copyright 2024 Martin Pool

use std::env;
use std::fs::{create_dir_all, File};
use std::io::Write;
use std::path::PathBuf;

fn main() {
    let out_dir = PathBuf::from(env::var_os("OUT_DIR").expect("OUT_DIR should be set by cargo"));
    let config_dir = out_dir.join("include");
    create_dir_all(&config_dir).unwrap();

    File::create(config_dir.join("config.h"))
        .expect("create config.h")
        .write_all("#define HAVE_VA_COPY\n".as_bytes())
        .expect("write config.h");
    cc::Build::new()
        .includes(["src", "lzo"])
        .include(&config_dir)
        .files([
            "lzo/minilzo.c",
            "src/compress.c",
            "src/io.c",
            "src/rustglue.c",
            "src/trace.c",
        ])
        .compile("distcc-rs");
    let bindings = bindgen::Builder::default()
        .header("src/rustbindings.h")
        .parse_callbacks(Box::new(bindgen::CargoCallbacks::new()))
        // .clang_arg("-Isrc")
        // .clang_arg("-Ilzo")
        // .clang_arg(format!("-I{}", config_dir.display()))
        .generate()
        .expect("Unable to generate bindings");
    bindings
        .write_to_file(out_dir.join("bindings.rs"))
        .expect("write bindings.rs");
}
