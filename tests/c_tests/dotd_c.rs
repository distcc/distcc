//! Test `dotd.c`, finding the `.d` dependency output file.

use std::ffi::c_char;
use std::os::raw::c_void;
use std::ptr::null_mut;

use distcc::c;
use distcc::glue::malloc::{alloc_argv, cstr_to_string, free_argv};
use libc::{c_int, free};

#[derive(Debug, PartialEq, Eq)]
struct DotdInfo {
    needs_dotd: bool,
    dotd_filename: Option<String>,
    dotd_target: Option<String>,
}

/// `argv` has no command, only the arguments.
fn get_dotd_info(argv: &[&str]) -> Result<DotdInfo, c_int> {
    let mut needs_dotd: c_int = 0;
    let mut sets_dotd_target: c_int = 0;
    let mut c_dotd_filename: *mut c_char = null_mut();
    let mut c_dotd_target: *mut c_char = null_mut();

    let c_argv = alloc_argv(argv.iter()).1;

    let ret;
    unsafe {
        ret = c::dcc_get_dotd_info(
            c_argv,
            &mut c_dotd_filename,
            &mut needs_dotd,
            &mut sets_dotd_target,
            &mut c_dotd_target,
        );
        free_argv(c_argv);
        if ret != 0 {
            Err(ret)
        } else {
            let dotd_filename = cstr_to_string(c_dotd_filename);
            let needs_dotd = needs_dotd != 0;
            let dotd_target = if sets_dotd_target != 0 {
                cstr_to_string(c_dotd_target)
            } else {
                None
            };
            free(c_dotd_filename as *mut c_void);
            free(c_dotd_target as *mut c_void);
            Ok(DotdInfo {
                needs_dotd,
                dotd_filename,
                dotd_target,
            })
        }
    }
}

#[test]
fn dotd() {
    // By default, no .d file is produced.
    assert_eq!(
        get_dotd_info(&["foo.c", "-o", "foo.o"]),
        Ok(DotdInfo {
            needs_dotd: false,
            dotd_filename: Some("foo.d".to_owned()),
            dotd_target: None,
        })
    );

    // TODO: set $DEPENDENCIES_OUTPUT
}
