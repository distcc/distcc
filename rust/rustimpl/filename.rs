//! Filename handling functions, ported from filename.c.

use std::ffi::{c_char, c_int, CStr};
use std::path::Path;
use std::ptr::{null, null_mut};

/// Return true if the file extension identifies it as source file.
#[unsafe(no_mangle)]
extern "C" fn dcc_is_source(sfile: *const c_char) -> c_int {
    let sfile_str = unsafe { CStr::from_ptr(sfile) };
    let path = Path::new(sfile_str.to_str().unwrap());
    // The C code also matched .s and .S when ENABLE_REMOTE_ASSEMBLE was on, but that did
    // not seem to ever be turned on, and the tests assert that they're not matched.
    path.extension()
        .and_then(|path| path.to_str())
        .is_some_and(|ext| {
            matches!(
                ext,
                "C" | "M"
                    | "c"
                    | "c++"
                    | "cc"
                    | "cp"
                    | "cpp"
                    | "cxx"
                    | "h"
                    | "h++"
                    | "hh"
                    | "hpp"
                    | "hxx"
                    | "m"
                    | "mi"
                    | "mii"
                    | "mm"
                    | "ii"
                    | "i"
            )
        })
        .into()
}

/**
 * Return a pointer to the extension (within the supplied buffer), including the dot, or NULL.
 * A filename ending in `.` returns NULL.
 **/
#[unsafe(no_mangle)]
extern "C" fn dcc_find_extension(path: *mut c_char) -> *mut c_char {
    let path_str = unsafe { CStr::from_ptr(path) }.to_str().unwrap();
    if let Some(dot_position) = path_str.rfind('.') {
        if (dot_position + 1) < path_str.len() {
            unsafe { path.add(dot_position) }
        } else {
            // The dot is the last character in the string, so there is no extension.
            null_mut()
        }
    } else {
        null_mut()
    }
}

#[unsafe(no_mangle)]
extern "C" fn dcc_find_extension_const(path: *const c_char) -> *const c_char {
    dcc_find_extension(path as *mut c_char)
}

/// Return a static string indicating what extension would be produced by
/// preprocessing a file with extension `ext`.
///
/// The extension should include its leading dot, like `".c"`.
///
/// Returns null if the extension is unrecognized.
#[unsafe(no_mangle)]
extern "C" fn dcc_preproc_exten(source_ext: *const c_char) -> *const c_char {
    assert!(!source_ext.is_null());
    let ext = unsafe { CStr::from_ptr(source_ext) }.to_str().unwrap();
    let pp = match ext {
        ".c" | ".i" => ".i",
        ".cc" | ".cpp" | ".cxx" | ".cp" | ".c++" | ".C" | ".ii" => ".ii",
        ".m" | ".mi" => ".mi",
        ".mm" | ".mii" | ".M" => ".mii",
        ".s" => ".s",
        _ => {
            return null();
        }
    };
    pp.as_ptr().cast()
}
