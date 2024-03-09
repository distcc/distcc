// Copyright 2024 Martin Pool

use std::env::args;
use std::process::ExitCode;

use distcc::argv::argv_to_c;
use distcc::c;

fn main() -> ExitCode {
    let (argc, argv) = argv_to_c(args());
    let err: u8 = unsafe { c::distcc_main(argc, argv) }
        .try_into()
        .expect("Exit code from C fits in u8");
    dbg!(err);
    ExitCode::from(err)
}
