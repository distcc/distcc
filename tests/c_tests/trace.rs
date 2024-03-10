// Copyright 2024 Martin Pool

use std::ffi::CStr;

use distcc::c;

#[test]
fn program_name_from_global() {
    let name = unsafe {
        CStr::from_ptr(c::rs_program_name)
            .to_str()
            .expect("Program name should be UTF-8")
    };
    assert_eq!(name, "distcc-c-tests");
}
