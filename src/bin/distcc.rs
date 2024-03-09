// Copyright 2024 Martin Pool

use std::ffi::{c_char, c_int, CString};
use std::iter::once;
use std::process::ExitCode;
use std::ptr::null_mut;

use distcc::c;

fn main() -> ExitCode {
    let args = std::env::args()
        .map(|arg| CString::new(arg).unwrap())
        .collect::<Vec<CString>>(); // kept for lifetime
    let argc = args.len();
    let mut c_args = args
        .into_iter()
        .map(|arg| arg.into_raw())
        .chain(once(null_mut()))
        .collect::<Vec<*mut c_char>>();
    let err: u8 = unsafe {
        c::distcc_main(argc as c_int, c_args.as_mut_ptr())
            .try_into()
            .expect("Exit code from C fits in u8")
    };
    dbg!(err);
    ExitCode::from(err)
}
