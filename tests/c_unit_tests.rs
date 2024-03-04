// Copyright 2024 Martin Pool

//! Tests from Rust into bound C code.

use std::ffi::CStr;
use std::io::{Read, Seek, Write};
use std::os::fd::AsRawFd;
use std::ptr::null_mut;
use std::slice;

use distcc::c;
use libc::c_uint;
use tempfile::tempfile;

// It's not nul terminated; we always pass the length.
static HELLO_WORLD: &[u8] = b"hello world";
static COMPRESSED_HELLO_WORLD: &[u8] = &[
    28, 104, 101, 108, 108, 111, 32, 119, 111, 114, 108, 100, 17, 0, 0,
];

#[test]
fn compress_lzo1_alloc() {
    let mut out_buf: *mut i8 = null_mut();
    let mut out_size = 0;

    let err = unsafe {
        c::dcc_compress_lzo1x_alloc(
            HELLO_WORLD.as_ptr() as *const i8,
            HELLO_WORLD.len(),
            (&mut out_buf) as *mut *mut i8,
            &mut out_size,
        )
    };
    assert_eq!(err, 0, "compression should succeed");
    dbg!(&out_buf, &out_size);
    let out_slice = unsafe { slice::from_raw_parts(out_buf, out_size) };
    assert_eq!(
        out_slice.iter().map(|x| *x as u8).collect::<Vec<u8>>(),
        COMPRESSED_HELLO_WORLD
    );
    unsafe {
        libc::free(out_buf as *mut libc::c_void);
    }
}

#[test]
fn decompress_lzo1x_to_from_files() {
    let mut input_file = tempfile().expect("create temp file");
    input_file
        .write_all(COMPRESSED_HELLO_WORLD)
        .expect("write temp file");
    input_file.rewind().expect("rewind temp file");
    let mut output_file = tempfile().expect("create temp file");
    let err = unsafe {
        c::dcc_r_bulk_lzo1x(
            output_file.as_raw_fd(),
            input_file.as_raw_fd(),
            COMPRESSED_HELLO_WORLD.len() as c_uint,
        )
    };
    assert_eq!(err, 0);
    output_file.rewind().expect("rewind temp file");
    let mut output = Vec::new();
    output_file
        .read_to_end(&mut output)
        .expect("read temp file");
    assert_eq!(output, HELLO_WORLD);
}

#[test]
fn program_name_from_global() {
    let name = unsafe {
        CStr::from_ptr(c::rs_program_name)
            .to_str()
            .expect("Program name should be UTF-8")
    };
    assert_eq!(name, "distcc");
}
