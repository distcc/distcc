//! Test `dotd.c`, finding the `.d` dependency output file.

use std::env::{remove_var, set_var};
use std::ffi::c_char;
use std::os::raw::c_void;
use std::ptr::null_mut;

use distcc::c;
use distcc::glue::malloc::{alloc_argv, cstr_to_string, free_argv};
use libc::{c_int, free};
use rusty_fork::rusty_fork_test;

#[derive(Debug, PartialEq, Eq)]
struct DotdInfo {
    needs_dotd: bool,
    dotd_filename: Option<String>,
    sets_dotd_target: bool,
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
            let sets_dotd_target = sets_dotd_target != 0;
            let dotd_target = cstr_to_string(c_dotd_target);
            free(c_dotd_filename as *mut c_void);
            // In the C implementation, *dotd_target can point into the middle
            // of an allocation and so can't be freed (and must be leaked)!
            // free(c_dotd_target as *mut c_void);
            Ok(DotdInfo {
                needs_dotd,
                dotd_filename,
                sets_dotd_target,
                dotd_target,
            })
        }
    }
}

fn dotd_case(
    command: &str,
    needs_dotd: bool,
    dotd_filename: Option<&str>,
    sets_dotd_target: bool,
    dotd_target: Option<&str>,
) {
    assert_eq!(
        get_dotd_info(
            command
                .split_ascii_whitespace()
                .collect::<Vec<_>>()
                .as_slice()
        ),
        Ok(DotdInfo {
            needs_dotd,
            dotd_filename: dotd_filename.map(String::from),
            sets_dotd_target,
            dotd_target: dotd_target.map(String::from),
        }),
        "{command}"
    );
}

rusty_fork_test! {
    #[test]
    fn dotd_without_dependencies_env() {
        remove_var("DEPENDENCIES_OUTPUT");
        remove_var("SUNPRO_DEPENDENCIES"); // not used in dotd.c, but respected by gcc.

        // By default, no .d file is produced.
        dotd_case("foo.c -c -o foo.o", false, Some("foo.d"), false, None); // .d filename is set even though it's not written?
        dotd_case("foo.c -c -o foo.o -MD", true, Some("foo.d"), false, None);
        dotd_case("foo.c -c -o foo.o -MMD", true, Some("foo.d"), false, None);
        dotd_case("foo.c -c -o foo.o -MF foo.dep", true, Some("foo.dep"), false, None);
        dotd_case("foo.c -c -o foo.o -MF foo.dep", true, Some("foo.dep"), false, None);
        dotd_case("foo.c -o foo -MD", true, Some("foo.d"), false, None);
        dotd_case("foo.D -o foo.o -c -MF foo.dep", true, Some("foo.dep"), false, None);

        // The C implementation does not seem to collect the makefile target
        // from -MT target, which seems like a bug.
        dotd_case("foo.c -o foo.o -c -MF foo.dep -MT target", true, Some("foo.dep"), true, None); // last should be Some("target"));
        dotd_case("foo.c -o foo.o -c -MF foo.dep -MQ target", true, Some("foo.dep"), true, None); // last should be Some("target"));

        // TODO: More cases from testdistcc.py.
        // TODO: set $DEPENDENCIES_OUTPUT.
    }

    #[test]
    fn dependencies_from_env_without_target() {
        set_var("DEPENDENCIES_OUTPUT", "foo.d");
        dotd_case("foo.c -c -o foo.o", true, Some("foo.d"), false, None);
    }

    #[test]
    fn dependencies_from_env_with_target() {
        // https://gcc.gnu.org/onlinedocs/gcc/Environment-Variables.html
        // The format is "DEPENDENCY_FILENAME TARGET_FILENAME".
        set_var("DEPENDENCIES_OUTPUT", "foo.d foo.o");
        // According to the comment on dcc_get_dotd_info,
        // sets_dotd_target should is false when the target is given
        // in the environment variable.
        dotd_case("foo.c -c -o foo.o", true, Some("foo.d"), false, Some("foo.o"));
    }
}
