// Copyright 2024 Martin Pool

//! Tests corresponding to `h_issource` and `IsSource_case`, covering some of `filename.c`.

use std::ffi::CStr;

use distcc::c;

fn is_source(path: &CStr) -> bool {
    unsafe { c::dcc_is_source(path.as_ptr()) != 0 }
}

fn is_preprocessed(path: &CStr) -> bool {
    unsafe { c::dcc_is_preprocessed(path.as_ptr()) != 0 }
}

#[test]
fn is_source_cases() {
    for name in [
        c"hello.c",
        c"hello.cc",
        c"hello.cxx",
        c"hello.c++",
        c"hello.m",
        c"hello.M",
        c"hello.mm",
        c"hello.mi",
        c"hello.mii",
        c"hello.2.4.4.i",
        c"hello.ii",
    ] {
        assert!(is_source(name), "{name:?} should be recognized as source");
    }
    for name in [c".foo", c"gcc", c"boot.s", c"boot.S"] {
        assert!(
            !is_source(name),
            "{name:?} should not be recognized as source"
        );
    }
}

#[test]
fn is_preprocessed_cases() {
    for name in [
        c"hello.c",
        c"hello.cc",
        c"hello.cxx",
        c"hello.c++",
        c"hello.m",
        c"hello.M",
        c"hello.mm",
        c".foo",
        c"gcc",
        c"boot.s",
        c"boot.S",
    ] {
        assert!(
            !is_preprocessed(name),
            "{name:?} should be recognized as not preprocessed"
        );
    }
    for name in [
        c"hello.2.4.4.i",
        c"hello.i",
        c"hello.ii",
        c"hello.mi",
        c"hello.mii",
    ] {
        assert!(
            is_preprocessed(name),
            "{name:?} should be recognized as preprocessed"
        );
    }
}
