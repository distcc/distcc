// Copyright 2024 Martin Pool

//! distcc internal library, shared between client and server.
//!
//! Includes C bindings to legacy code.

/// FFI to legacy C code.
pub mod c {
    #![allow(non_upper_case_globals)]
    #![allow(non_camel_case_types)]
    #![allow(non_snake_case)]
    #![allow(unused)] // Many C things are declared and not yet used.
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}
