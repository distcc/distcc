// Copyright 2024 - 2024 Martin Pool

//! Connect distcc's C-side trace output to Rust tracing.

use std::ffi::{c_char, c_int, c_void, CStr};
use std::ptr::null_mut;
use std::sync::Mutex;

use tracing::Level;

use crate::c::{self, __va_list_tag};

static TRACE_CONFIGURED: Mutex<bool> = Mutex::new(false);

/// Send C-side messages to Rust's tracing framework, and turn off C tracing.
///
/// Once turned on it cannot be turned off, and it can only be initialized once.
///
/// # Panics
///
/// * If internal state is corrupted.
pub fn glue_trace() {
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
unsafe extern "C" fn trace_message_to_logging(
    _flags: c_int,
    function_name: *const c_char,
    msg: *const c_char,
    _va_list: *mut __va_list_tag,
    _private_ptr: *mut c_void,
    _private_int: c_int,
) {
    let fn_str = if function_name.is_null() {
        None
    } else {
        let cstr = unsafe { CStr::from_ptr(function_name) };
        Some(cstr.to_str().expect("function_name is not UTF-8"))
    };
    debug_assert!(!msg.is_null(), "trace message is null");
    // let level = match (flags as u32) & RS_LOG_PRIMASK {
    //     _ => tracing::Level::DEBUG,
    // };
    let msg_str = unsafe { CStr::from_ptr(msg) }.to_str().unwrap();
    // TODO: Get the right level; might require constructing a tracing Metadata object.
    // TODO: Expand the format string from the va_list
    if let Some(fn_str) = fn_str {
        tracing::event!(target: "distcc", Level::DEBUG, "{fn_str}: {msg_str}");
    } else {
        tracing::event!(target: "distcc", Level::DEBUG, "{msg_str}");
    }
}
