// Copyright 2024 Martin Pool

//! FFI tests for `src/util.c`

use std::ffi::c_char;
use std::path::PathBuf;
use std::ptr::null_mut;

use distcc::c;

/// On the path from the run environment we should find some cc.
#[test]
fn dcc_which_finds_system_cc() {
    let mut found_path: *mut c_char = null_mut();
    let ret = unsafe { c::dcc_which(c"cc".as_ptr(), &mut found_path as *mut *mut c_char) };
    assert_eq!(ret, 0);
    assert!(!found_path.is_null());
    dbg!(unsafe { std::ffi::CStr::from_ptr(found_path) });
    let path_cstr = unsafe { std::ffi::CStr::from_ptr(found_path) };
    let path = PathBuf::from(&path_cstr.to_str().unwrap());
    assert!(path.exists());
    assert!(path.ends_with("cc"));
}
