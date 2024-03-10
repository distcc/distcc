use std::ffi::{c_int, CStr};
use std::path::Path;

// int dcc_is_source(const char *sfile)
#[no_mangle]
fn dcc_is_source(sfile: &CStr) -> c_int {
    let path = Path::new(sfile.to_str().unwrap());
    // The C code also matched .s and .S when ENABLE_REMOTE_ASSEMBLE was on, but that did
    // not seem to ever be turned on, and the tests assert that they're not matched.
    path.extension()
        .and_then(|path| path.to_str())
        .map_or(false, |ext| {
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
