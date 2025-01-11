// Copyright 2024 - 2024 Martin Pool

//! Connect distcc's C-side trace output to Rust tracing.

use std::ffi::{c_char, c_int, c_void, CStr};
use std::ptr::null_mut;
use std::sync::Mutex;

use libc::{malloc, size_t};
use tracing::Level;

use crate::c::{self, __va_list_tag, RS_LOG_NO_PROGRAM};

static TRACE_CONFIGURED: Mutex<bool> = Mutex::new(false);

/// Send C-side messages to Rust's tracing framework, and turn off C tracing.
///
/// Once turned on it cannot be turned off, and it can only be initialized once.
///
/// # Panics
///
/// * If internal state is corrupted.
pub fn route_c_trace_to_rust() {
    let mut configured = TRACE_CONFIGURED.lock().expect("trace lock poisoned");
    if !*configured {
        *configured = true;
        unsafe {
            c::rs_remove_all_loggers();
            c::rs_trace_set_level(c::rs_loglevel_RS_LOG_DEBUG);
            c::rs_add_logger(
                Some(trace_message_to_logging),
                c::rs_loglevel_RS_LOG_DEBUG.try_into().unwrap(), // statically safe, just the wrong type in the C header
                null_mut(),
                0,
            );
        }
    }
}

/// Callback from C to Rust to emit messages.
///
/// This implements the C prototype `rs_logger_fn`.
#[allow(clippy::cast_possible_wrap, clippy::cast_sign_loss)]
unsafe extern "C" fn trace_message_to_logging(
    flags: c_int,
    function_name: *const c_char,
    msg_template: *const c_char,
    va_list: *mut __va_list_tag,
    _private_ptr: *mut c_void,
    _private_int: c_int,
) {
    const MAX_LEN: size_t = 1024;
    let fn_str = if function_name.is_null() {
        None
    } else {
        let cstr = unsafe { CStr::from_ptr(function_name) };
        Some(cstr.to_str().expect("function_name is not UTF-8"))
    };
    debug_assert!(!msg_template.is_null(), "trace message is null");
    // let level = match (flags as u32) & RS_LOG_PRIMASK {
    //     _ => tracing::Level::DEBUG,
    // };
    let mut msg = fn_str.map_or_else(String::new, |s| format!("{s}: "));
    let formatted = unsafe {
        let buf: *mut c_char = malloc(MAX_LEN).cast();
        assert!(!buf.is_null(), "malloc failed");
        // TODO: maybe also RS_LOG_NO_PID
        c::rs_format_msg(
            buf,
            MAX_LEN,
            (flags as u32 | RS_LOG_NO_PROGRAM) as i32,
            function_name,
            msg_template,
            va_list,
        );
        CStr::from_ptr(buf).to_str().expect("msg is not UTF-8")
    };
    msg += formatted;
    // TODO: Get the right level; might require constructing a tracing Metadata object.
    tracing::event!(target: "distcc", Level::DEBUG, "{msg}");
}
