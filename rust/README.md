# distcc/rust

This directory contains a partial reimplementation of distcc into Rust.

As of early 2025 this is an experiment or learning exercise and it may never land.

## Approach

The general approach so far is to convert the `main` entrypoints from C into Rust. This is in `rust/bin/`. These main functions then call into mostly C code, which calls back into Rust code for the functions that have been converted so far.

The attraction of this approach is that it allows the top-level binaries to be built by Cargo.

The general idea is to translate one file at a time from C to Rust, keeping the same API as much as possible and making minimal changes to the C code that calls it, until the file can be deleted. At the same time we add new tests in Rust.
When blocks of Rust code emerge that are only called from Rust, not from C, those interfaces can change to Rust APIs.

`glue` contains some convenience functions for writing bindings: specifically at the moment for translating between malloced and Rust-allocated strings and argv vectors.

`rustimpl` contains Rust interfaces that are callable from C.

## Building

In the distcc source directory just

    cargo build

This should automatically generating bindings between C and Rust.
