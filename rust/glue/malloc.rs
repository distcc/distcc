//! Allocate objects from Rust on the C heap, so that they can later be freed by C.
//!
//! Rust uses its own allocator, typically, so objects from CString etc typically
//! can't safely be released by `free`.

use std::ffi::{c_char, c_int};
use std::mem::size_of;

use libc::{calloc, strndup};

/// Allocate a copy of a Rust string on the C heap.
pub fn alloc_string(s: &str) -> *mut c_char {
    unsafe { strndup(s.as_ptr() as *const c_char, s.len()) }
}

/// Allocate an argv-style array of strings terminated by null on the C heap.
///
/// Both the strings and the argv array are allocated.
///
/// Returns (argc, argv).
pub fn alloc_argv<I, S>(args: I) -> (c_int, *mut *mut c_char)
where
    I: ExactSizeIterator<Item = S>,
    S: AsRef<str>,
{
    let argc = args.len();
    let argv = unsafe { calloc(argc + 1, size_of::<*mut c_char>()) as *mut *mut c_char };
    for (i, s) in args.into_iter().enumerate() {
        let p = alloc_string(s.as_ref());
        unsafe { *argv.add(i) = p };
    }
    (argc as c_int, argv)
}

#[cfg(test)]
mod test {
    use std::ffi::CStr;
    use std::ptr::null_mut;

    use libc::c_void;

    #[test]
    fn test_alloc_string() {
        let s = super::alloc_string("hello");
        assert_eq!(unsafe { CStr::from_ptr(s).to_str().unwrap() }, "hello");
        unsafe { libc::free(s as *mut libc::c_void) };
    }

    #[test]
    fn test_alloc_argv() {
        let (argc, argv) = super::alloc_argv(["hello", "world"].iter());
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
        unsafe {
            libc::free(*argv as *mut c_void);
            libc::free(*argv.add(1) as *mut c_void);
            libc::free(argv as *mut libc::c_void)
        };
    }
}
