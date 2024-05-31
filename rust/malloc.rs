use std::ffi::c_char;

use libc::strndup;

/// Allocate a copy of a Rust string on the C heap.
pub fn alloc_string(s: &str) -> *mut c_char {
    unsafe { strndup(s.as_ptr() as *const c_char, s.len()) }
}
