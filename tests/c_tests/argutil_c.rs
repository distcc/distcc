//! Rust tests for `argutil.c`.

use std::ffi::CStr;

use libc::{c_void, free};

use distcc::c::dcc_argv_tostr;
use distcc::glue::malloc::alloc_argv;

#[test]
fn test_dcc_argv_tostr() {
    let argv = ["hello", "shiny new", "tab\tseparated", "'quoted'", "world"];
    let (_argc, argv) = alloc_argv(argv.iter());
    let s = unsafe { dcc_argv_tostr(argv) };
    let sstr = unsafe { CStr::from_ptr(s) };
    assert_eq!(
        sstr.to_str(),
        Ok("hello \"shiny new\" \"tab\tseparated\" \"'quoted'\" world")
    );
    unsafe { free(s as *mut c_void) };
}
