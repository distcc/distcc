// Copyright 2024 Martin Pool

//! Tests from Rust into bound C code.

use std::ffi::CStr;

mod c_tests;

#[allow(non_upper_case_globals, unused)]
#[no_mangle]
static rs_program_name: &CStr = c"distcc-c-tests";
