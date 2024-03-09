// Copyright 2024 Martin Pool

//! FFI tests for `src/util.c`

use std::ffi::{c_char, c_int, CString};
use std::path::PathBuf;
use std::ptr::null_mut;

// use rusty_fork::rusty_fork_test;

use distcc::c;

fn dcc_which(name: &str) -> Result<PathBuf, c_int> {
    let mut found_path: *mut c_char = null_mut();
    let c_name = CString::new(name).unwrap();
    let ret = unsafe { c::dcc_which(c_name.as_ptr(), &mut found_path as *mut *mut c_char) };
    if ret == 0 {
        assert!(!found_path.is_null());
        let path_cstr = unsafe { std::ffi::CStr::from_ptr(found_path) };
        let path = PathBuf::from(&path_cstr.to_str().unwrap());
        unsafe { libc::free(found_path as *mut std::ffi::c_void) };
        Ok(path)
    } else {
        assert!(found_path.is_null());
        Err(ret)
    }
}

/// On the path from the run environment we should find some cc.
#[test]
fn dcc_which_finds_system_cc() {
    assert_eq!(dcc_which("cc").unwrap().file_name().unwrap(), "cc");
}

/// Test the case described in <https://github.com/distcc/distcc/pull/500> and
/// #497: if the compiler is not found, distcc will segfault, but
/// actually it should return an error.
///
/// Before that was fixed, this test reproduced the segfault.
#[test]
fn dcc_which_nonexistent_command_not_found() {
    assert_eq!(dcc_which("cc_____NONEXISTENT_____1234").unwrap_err(), -libc::ENOENT);
}

// rusty_fork_test! {
//     // /// Path components containing "distcc" are not matched.
//     // #[test]
//     // fn dcc_which_skips_distcc_directories() {
//     // }
// }
