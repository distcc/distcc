//! Control distcc C trace facilities from Rust

use std::ptr::null_mut;
use std::sync::Mutex;

use crate::c;

static TRACE_CONFIGURED: Mutex<bool> = Mutex::new(false);

/// Turn on trace to stderr, at the 'debug' level.
///
/// For now, once turned on it cannot be turned off.
///
/// # Panics
///
/// * If internal state is corrupted.
pub fn trace_to_stderr() {
    let mut lock = TRACE_CONFIGURED.lock().expect("trace lock poisoned");
    if !*lock {
        *lock = true;
        unsafe {
            c::rs_trace_set_level(c::rs_loglevel_RS_LOG_DEBUG);
            c::rs_add_logger(
                Some(c::rs_logger_file),
                c::rs_loglevel_RS_LOG_DEBUG.try_into().unwrap(), // statically safe, just the wrong type in the C header
                null_mut(),
                2, /* stderr */
            );
        }
    }
}
