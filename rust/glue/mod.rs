// Copyright 2024-2025 Martin Pool

//! Glue code between Rust and C.

#![warn(clippy::pedantic)]
#![allow(clippy::missing_safety_doc)]

use std::env::args;
use std::ffi::{c_char, c_int, CStr};
use std::process::ExitCode;

use malloc::alloc_argv;
use trace::route_c_trace_to_rust;
use tracing_subscriber::{fmt, prelude::*, EnvFilter};

pub mod malloc;
pub mod trace;

pub(crate) unsafe fn cstr_to_owned(s: *const c_char) -> Option<String> {
    unsafe {
        if s.is_null() {
            None
        } else {
            Some(CStr::from_ptr(s).to_str().unwrap().to_owned())
        }
    }
}

/// General glue around a C `main` function.
///
/// This sets up Rust tracing and converts Rust arguments to C.
#[allow(clippy::similar_names)]
pub fn wrap_main(main: unsafe extern "C" fn(c_int, *mut *mut c_char) -> c_int) -> ExitCode {
    // TODO: The C code later registers its own trace fn, so the messages all get duplicated. Perhaps we can just stop doing that from C.
    route_c_trace_to_rust();
    tracing_subscriber::registry()
        .with(fmt::layer())
        .with(EnvFilter::from_default_env())
        .init();
    let (argc, argv) = alloc_argv(args());
    let err: u8 = unsafe { main(argc, argv) }.try_into().unwrap_or(0x7f);
    dbg!(err);
    ExitCode::from(err)
}
