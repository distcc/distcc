//! Manipulation of C argv lists.

use std::ffi::{c_char, c_int, CStr};
use std::mem::size_of;

use libc::calloc;

use crate::malloc::alloc_string;

/// Return true if the argv contains any element equal to `needle`.
///
/// # Safety
/// argv must be a null-terminated array of C strings; needle must be a valid C string.
#[no_mangle]
pub unsafe extern "C" fn argv_contains(argv: *mut *mut c_char, needle: *const c_char) -> c_int {
    let needle = unsafe { CStr::from_ptr(needle) };
    let mut i = 0;
    loop {
        let arg = unsafe { *argv.offset(i) };
        if arg.is_null() {
            return false as c_int;
        }
        if unsafe { CStr::from_ptr(arg) } == needle {
            return true as c_int;
        }
        i += 1;
    }
}

/// Make a new malloc'd argv array of strings terminated by null on the C heap.
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

/// Convert C argv list to a Vec of newly allocated Rust strings.
///
/// The args are copied, not moved.
///
/// # Safety
///
/// `argv` must point to an array of pointers to null-terminated strings.
pub unsafe fn argv_to_vec(argv: *mut *mut c_char) -> Vec<String> {
    (0..)
        .map(|i| *argv.add(i))
        .take_while(|a| !a.is_null())
        .map(|a| CStr::from_ptr(a).to_str().unwrap().to_owned())
        .collect()
}

/// Free an arg list whose entries are themselves allocated on the C heap.
///
/// # Safety
///
/// `argv` must point to a malloced array of pointers to malloced strings, terminated by a null.
#[no_mangle]
pub unsafe extern "C" fn dcc_free_argv(argv: *mut *mut c_char) {
    unsafe {
        for i in 0.. {
            let arg = *argv.add(i);
            if arg.is_null() {
                break;
            } else {
                libc::free(arg as *mut libc::c_void);
            }
        }
        libc::free(argv as *mut libc::c_void);
    }
}

/// Return the length of an argv list, not counting the null terminator.
///
/// # Safety
/// `argv` must point to a null-terminated array of pointers.
#[no_mangle]
pub unsafe extern "C" fn dcc_argv_len(argv: *mut *mut c_char) -> c_int {
    for i in 0.. {
        if (*argv.add(i)).is_null() {
            return i as c_int;
        }
    }
    unreachable!("argv list was not null-terminated");
}

// TODO: argv_append, copy_argv

#[cfg(test)]
mod test {
    use std::ffi::CStr;
    use std::iter::empty;
    use std::ptr::null_mut;

    use super::*;

    #[test]
    fn test_alloc_string() {
        let s = super::alloc_string("hello");
        assert_eq!(unsafe { CStr::from_ptr(s).to_str().unwrap() }, "hello");
        unsafe { libc::free(s as *mut libc::c_void) };
    }

    #[test]
    fn test_argv_contains() {
        let (argc, argv) = alloc_argv(["hello", "world"].iter());
        assert_eq!(argc, 2);
        assert!(unsafe { argv_contains(argv, c"hello".as_ptr()) == 1 },);
        assert!(unsafe { argv_contains(argv, c"world".as_ptr()) == 1 },);
        assert!(unsafe { argv_contains(argv, c"goodbye".as_ptr()) == 0 },);
        assert!(unsafe { argv_contains(argv, c"hello".as_ptr()) == 1 },);
        assert!(unsafe { argv_contains(argv, c"world".as_ptr()) == 1 },);
        assert!(unsafe { argv_contains(argv, c"goodbye".as_ptr()) == 0 },);
        unsafe { dcc_free_argv(argv) };
    }

    #[test]
    fn empty_argv() {
        let (argc, argv) = alloc_argv(empty::<&str>());
        assert_eq!(argc, 0);
        assert!(!argv.is_null());
        assert!(unsafe { (*argv).is_null() });
        assert_eq!(unsafe { dcc_argv_len(argv) }, 0);
        unsafe { dcc_free_argv(argv) };
    }

    #[test]
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
        assert_eq!(unsafe { dcc_argv_len(argv) }, 2);
        unsafe { dcc_free_argv(argv) };
    }
}
