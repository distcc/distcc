// Copyright 2024 - 2025 Martin Pool

//! Rust entry point for the distcc client.

#![warn(rust_2024_compatibility)]

use std::env::args;
use std::ffi::CStr;
use std::process::ExitCode;

use tracing_subscriber::{fmt, prelude::*, EnvFilter};

use distcc::c;
use distcc::glue::malloc::alloc_argv;

#[allow(non_upper_case_globals, unused)]
#[unsafe(no_mangle)]
static rs_program_name: &CStr = c"distcc";

fn main() -> ExitCode {
    tracing_subscriber::registry()
        .with(fmt::layer())
        .with(EnvFilter::from_default_env())
        .init();
    let (argc, argv) = alloc_argv(args());
    let err: u8 = unsafe { c::distcc_main(argc, argv) }
        .try_into()
        .expect("Exit code from C fits in u8");
    dbg!(err);
    ExitCode::from(err)
}
