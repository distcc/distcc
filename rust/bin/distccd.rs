// Copyright 2024 Martin Pool

#![warn(rust_2024_compatibility)]

use std::env::args;
use std::ffi::CStr;
use std::process::ExitCode;

use distcc::glue::trace::glue_trace;
use tracing_subscriber::{fmt, prelude::*, EnvFilter};

use distcc::c;
use distcc::glue::malloc::alloc_argv;

#[allow(non_upper_case_globals, unused)]
#[unsafe(no_mangle)]
static rs_program_name: &CStr = c"distccd";

fn main() -> ExitCode {
    tracing_subscriber::registry()
        .with(fmt::layer())
        .with(EnvFilter::from_default_env())
        .init();
    glue_trace();
    let (argc, argv) = alloc_argv(args());
    let err: u8 = unsafe { c::distccd_main(argc, argv) }
        .try_into()
        .expect("Exit code from C fits in u8");
    dbg!(err);
    ExitCode::from(err)
}
