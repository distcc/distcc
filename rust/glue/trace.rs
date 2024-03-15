//! Control distcc C trace facilities from Rust

use std::ptr::null_mut;
use std::sync::Mutex;

use crate::c;

static TRACE_CONFIGURED: Mutex<bool> = Mutex::new(false);

/// Turn on trace to stderr, at the 'debug' level.
///
/// For now, once turned on it cannot be turned off.
pub fn trace_to_stderr() {
    let mut lock = TRACE_CONFIGURED.lock().unwrap();
    if !*lock {
        *lock = true;
        unsafe {
            c::rs_trace_set_level(c::rs_loglevel_RS_LOG_DEBUG);
            c::rs_add_logger(
                Some(c::rs_logger_file),
                c::rs_loglevel_RS_LOG_DEBUG as i32,
                null_mut(),
                2, /* stderr */
            );
        }
    }
}
