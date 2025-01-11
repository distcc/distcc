// Copyright 2024 - 2025 Martin Pool

//! Rust entry point for the distcc client.

#![warn(rust_2024_compatibility)]

use std::ffi::CStr;
use std::process::ExitCode;

use distcc::c;
use distcc::glue::wrap_main;

#[allow(non_upper_case_globals, unused)]
#[unsafe(no_mangle)]
static rs_program_name: &CStr = c"distcc";

fn main() -> ExitCode {
    wrap_main(c::distcc_main)
}
