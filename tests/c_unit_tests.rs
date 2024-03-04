// Copyright 2024 Martin Pool

//! Tests from Rust into bound C code.

use std::ffi::CStr;
use std::ptr::null_mut;
use std::slice;

use distcc::c;

#[test]
fn compress_lzo1_alloc() {
    let input = b"hello world";
    let mut out_buf: *mut i8 = null_mut();
    let mut out_size = 0;

    let err = unsafe {
        c::dcc_compress_lzo1x_alloc(
            input.as_ptr() as *const i8,
            input.len(),
            (&mut out_buf) as *mut *mut i8,
            &mut out_size,
        )
    };
    assert_eq!(err, 0, "compression should succeed");
    dbg!(&out_buf, &out_size);
    let out_slice = unsafe { slice::from_raw_parts(out_buf, out_size) };
    assert_eq!(
        out_slice,
        [28, 104, 101, 108, 108, 111, 32, 119, 111, 114, 108, 100, 17, 0, 0,]
    );
}

#[test]
fn program_name_from_global() {
    let name = unsafe {
        CStr::from_ptr(c::rs_program_name)
            .to_str()
            .expect("Program name should be UTF-8")
    };
    assert_eq!(name, "distcc(rust)");
}
