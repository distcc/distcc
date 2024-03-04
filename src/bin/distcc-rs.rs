// Copyright 2024 Martin Pool

use std::ffi::{c_char, c_int, CString};
use std::process::ExitCode;

use distcc::c;

fn main() -> ExitCode {
    println!("Hello from distcc-rs!");

    let mut c_args = argv_to_c();
    let err: u8 = unsafe {
        c::distcc_main(
            c_args.len() as c_int,
            c_args.as_mut_ptr() as *mut *mut c_char,
        )
        .try_into()
        .expect("Exit code from C fits in u8")
    };
    dbg!(err);
    ExitCode::from(err)
}

fn argv_to_c() -> Vec<*const c_char> {
    // Map argv to C.
    let args = std::env::args()
        .map(|arg| CString::new(arg).unwrap())
        .collect::<Vec<CString>>();
    // convert the strings to raw pointers
    args.iter()
        .map(|arg| arg.as_ptr())
        .collect::<Vec<*const c_char>>()
}
