//! Glue code between Rust and C.

// #![allow(clippy::missing_safety_doc)]

use std::ffi::{c_char, CStr};

pub mod malloc;
pub mod trace;

pub(crate) unsafe fn cstr_to_owned(s: *const c_char) -> Option<String> {
    unsafe {
        if s.is_null() {
            None
        } else {
            Some(CStr::from_ptr(s).to_str().unwrap().to_owned())
        }
    }
}
