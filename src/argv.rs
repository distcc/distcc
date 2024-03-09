// Copyright 2024 Martin Pool

use std::ffi::{c_char, CString};
use std::iter::once;
use std::ptr::null_mut;

use libc::c_int;

/// Convert Rust arguments into an (argc, argv) pair suitable for passing to C.
///
/// This allocates a new array of strings, all of which are leaked.
pub fn argv_to_c<I>(args: I) -> (c_int, *mut *mut c_char)
where
    I: IntoIterator<Item = String>,
{
    let args = args
        .into_iter()
        .map(|arg| CString::new(arg).unwrap())
        .collect::<Vec<CString>>(); // kept for lifetime
    let argc = args.len();
    let argv = args
        .into_iter()
        .map(|arg| arg.into_raw())
        .chain(once(null_mut()))
        .collect::<Vec<*mut c_char>>()
        .leak();
    (argc as c_int, argv.as_mut_ptr())
}
