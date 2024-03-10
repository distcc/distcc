// Copyright 2024 Martin Pool

use std::env::args;
use std::ffi::CStr;
use std::process::ExitCode;

use distcc::argv::argv_to_c;
use distcc::c;

#[allow(non_upper_case_globals, unused)]
#[no_mangle]
static rs_program_name: &CStr = c"distccd";

fn main() -> ExitCode {
    let (argc, argv) = argv_to_c(args());
    let err: u8 = unsafe { c::distccd_main(argc, argv) }
        .try_into()
        .expect("Exit code from C fits in u8");
    dbg!(err);
    ExitCode::from(err)
}
