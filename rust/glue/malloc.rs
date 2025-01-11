//! Allocate objects from Rust on the C heap, so that they can later be freed by C.
//!
//! Rust uses its own allocator, typically, so objects from `CString` etc typically
//! can't safely be released by `free`.

use std::ffi::{c_char, c_int, CStr};
use std::mem::size_of;

use libc::{calloc, strndup};

/// Allocate a copy of a Rust string on the C heap.
///
/// # Panics
/// * If `strndup` fails.
#[must_use]
pub fn alloc_string(s: &str) -> *mut c_char {
    let p = unsafe { strndup(s.as_ptr().cast::<c_char>(), s.len()) };
    assert!(!p.is_null(), "strndup failed");
    p
}

/// Allocate an argv-style array of strings terminated by null on the C heap.
///
/// Both the strings and the argv array are allocated.
///
/// Returns (argc, argv).
///
/// # Panics
/// * If `from_args` is too long.
/// * If `calloc` fails.
#[must_use]
pub fn alloc_argv<I, S>(from_args: I) -> (c_int, *mut *mut c_char)
where
    I: ExactSizeIterator<Item = S>,
    S: AsRef<str>,
{
    let len = from_args.len();
    let argv = unsafe { calloc(len + 1, size_of::<*mut c_char>()).cast::<*mut c_char>() };
    assert!(!argv.is_null(), "calloc failed");
    for (i, s) in from_args.into_iter().enumerate() {
        let p = alloc_string(s.as_ref());
        unsafe { *argv.add(i) = p };
    }
    (len.try_into().unwrap(), argv)
}

/// Free an arg list whose entries are themselves allocated on the C heap.
///
/// # Safety
///
/// `argv` must point to a malloced array of pointers to malloced strings, terminated by a null.
pub unsafe fn free_argv(argv: *mut *mut c_char) {
    unsafe {
        for i in 0.. {
            let arg = *argv.add(i);
            if arg.is_null() {
                break;
            }
            libc::free(arg.cast());
        }
        libc::free(argv.cast());
    }
}

/// Convert C argv list of UTF-8 strings to a Vec of newly allocated Rust strings.
///
/// # Safety
///
/// `argv` must point to an array of pointers to null-terminated strings.
///
/// # Panics
///
/// * If any of the strings are not valid UTF-8.
#[allow(clippy::maybe_infinite_iter)] // it won't be infinite
pub unsafe fn argv_to_vec(argv: *mut *mut c_char) -> Vec<String> {
    (0..)
        .map(|i| *argv.add(i))
        .take_while(|a| !a.is_null())
        .map(|a| {
            CStr::from_ptr(a)
                .to_str()
                .expect("argv elements are UTF-8")
                .to_owned()
        })
        .collect()
}

/// Convert a UTF-8 C string to a Rust string, or null to None.
///
/// # Safety
///
/// `cstr` must point to a null-terminated string, or be null.
///
/// # Panics
///
/// * If the string is not UTF-8.
#[must_use]
pub unsafe fn cstr_to_string(cstr: *const c_char) -> Option<String> {
    if cstr.is_null() {
        None
    } else {
        Some(
            CStr::from_ptr(cstr)
                .to_str()
                .expect("String is not UTF-8")
                .to_owned(),
        )
    }
}

#[cfg(test)]
mod test {
    use std::ffi::CStr;
    use std::ptr::null_mut;

    use super::*;

    #[test]
    fn test_alloc_string() {
        let s = super::alloc_string("hello");
        assert_eq!(unsafe { CStr::from_ptr(s).to_str().unwrap() }, "hello");
        unsafe { libc::free(s.cast()) };
    }

    #[test]
    #[allow(clippy::similar_names)]
    fn test_alloc_argv() {
        let (argc, argv) = alloc_argv(["hello", "world"].iter());
        assert_eq!(argc, 2);
        assert_eq!(
            unsafe { CStr::from_ptr(*argv.add(0)).to_str().unwrap() },
            "hello"
        );
        assert_eq!(
            unsafe { CStr::from_ptr(*argv.add(1)).to_str().unwrap() },
            "world"
        );
        assert_eq!(unsafe { *argv.add(2) }, null_mut());
        assert_eq!(unsafe { argv_to_vec(argv) }, vec!["hello", "world"]);
        unsafe {
            libc::free((*argv).cast());
            libc::free((*argv.add(1)).cast());
            libc::free(argv.cast());
        };
    }
}
