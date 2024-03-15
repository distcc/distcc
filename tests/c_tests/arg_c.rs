//! Test C code from arg.c.
//!
//! Replaces old `h_scanargs` and ScanArgs_Case.

/*
class ScanArgs_Case(SimpleDistCC_Case):
    '''Test understanding of gcc command lines.'''
    def runtest(self):
        cases = [("gcc -c hello.c", "distribute", "hello.c", "hello.o"),
                 ("gcc hello.c", "local"),
                 ("gcc -o /tmp/hello.o -c ../src/hello.c", "distribute", "../src/hello.c", "/tmp/hello.o"),
                 ("gcc -DMYNAME=quasibar.c bar.c -c -o bar.o", "distribute", "bar.c", "bar.o"),
                 ("gcc -ohello.o -c hello.c", "distribute", "hello.c", "hello.o"),
                 ("ccache gcc -c hello.c", "distribute", "hello.c", "hello.o"),
                 ("gcc hello.o", "local"),
                 ("gcc -o hello.o hello.c", "local"),
                 ("gcc -o hello.o -c hello.s", "local"),
                 ("gcc -o hello.o -c hello.S", "local"),
                 ("gcc -fprofile-arcs -ftest-coverage -c hello.c", "local", "hello.c", "hello.o"),
                 ("gcc -S hello.c", "distribute", "hello.c", "hello.s"),
                 ("gcc -c -S hello.c", "distribute", "hello.c", "hello.s"),
                 ("gcc -S -c hello.c", "distribute", "hello.c", "hello.s"),
                 ("gcc -M hello.c", "local"),
                 ("gcc -ME hello.c", "local"),
                 ("gcc -MD -c hello.c", "distribute", "hello.c", "hello.o"),
                 ("gcc -MMD -c hello.c", "distribute", "hello.c", "hello.o"),

                 # Assemble to stdout (thanks Alexandre).
                 ("gcc -S foo.c -o -", "local"),
                 ("-S -o - foo.c", "local"),
                 ("-c -S -o - foo.c", "local"),
                 ("-S -c -o - foo.c", "local"),

                 # dasho syntax
                 ("gcc -ofoo.o foo.c -c", "distribute", "foo.c", "foo.o"),
                 ("gcc -ofoo foo.o", "local"),

                 # tricky this one -- no dashc
                 ("foo.c -o foo.o", "local"),
                 ("foo.c -o foo.o -c", "distribute", "foo.c", "foo.o"),

                 # Produce assembly listings
                 ("gcc -Wa,-alh,-a=foo.lst -c foo.c", "local"),
                 ("gcc -Wa,--MD -c foo.c", "local"),
                 ("gcc -Wa,-xarch=v8 -c foo.c", "distribute", "foo.c", "foo.o"),

                 # Produce .rpo files
                 ("g++ -frepo foo.C", "local"),

                 ("gcc -xassembler-with-cpp -c foo.c", "local"),
                 ("gcc -x assembler-with-cpp -c foo.c", "local"),

                 ("gcc -specs=foo.specs -c foo.c", "local"),

                 # Fixed in 2.18.4 -- -dr writes rtl to a local file
                 ("gcc -dr -c foo.c", "local"),
                 ]
        for tup in cases:
            self.checkScanArgs(*tup)

    def checkScanArgs(self, ccmd, mode, input=None, output=None):
        o, err = self.runcmd("h_scanargs %s" % ccmd)
        o = o[:-1]                      # trim \n
        os = o.split()
        if mode != os[0]:
            self.fail("h_scanargs %s gave %s mode, expected %s" %
                      (ccmd, os[0], mode))
        if mode == 'distribute':
            if os[1] != input:
                self.fail("h_scanargs %s gave %s input, expected %s" %
                          (ccmd, os[1], input))
            if os[2] != output:
                self.fail("h_scanargs %s gave %s output, expected %s" %
                          (ccmd, os[2], output))
*/

use std::ffi::CStr;
use std::iter::once;
use std::ptr::null_mut;

use distcc::c;
use distcc::glue::malloc::{alloc_argv, argv_to_vec, free_argv};

/// Evaluate if this command should be run local or remote.
///
/// If the command should be run remote, the input and output files are also returned.
fn scan_args(args: &[&str]) -> Result<(String, String, Vec<String>), u32> {
    let argv = alloc_argv(args.iter()).1;
    let mut arg1 = null_mut();
    let ret = unsafe { c::dcc_find_compiler(argv, &mut arg1) };
    assert_eq!(ret, 0, "dcc_find_compiler failed on {args:?}");

    let mut in_file = null_mut();
    let mut out_file = null_mut();
    let mut fixed_argv = null_mut();
    let ret = unsafe { c::dcc_scan_args(arg1, &mut in_file, &mut out_file, &mut fixed_argv) };
    unsafe {
        free_argv(argv);
    };
    if ret != 0 {
        Err(ret as u32)
    } else {
        assert!(!in_file.is_null());
        assert!(!out_file.is_null());
        assert!(!fixed_argv.is_null());
        let fixed_args = unsafe { argv_to_vec(fixed_argv) };
        let in_file = unsafe { CStr::from_ptr(in_file) }
            .to_str()
            .unwrap()
            .to_owned();
        let out_file = unsafe { CStr::from_ptr(out_file) }
            .to_str()
            .unwrap()
            .to_owned();
        unsafe {
            free_argv(fixed_argv);
        }
        Ok((in_file, out_file, fixed_args))
    }
}

#[test]
fn scan_args_cases() {
    // distcc::glue::trace::trace_to_stderr();
    let cases = [
        ("gcc -c hello.c", Ok(("hello.c", "hello.o"))),
        ("gcc hello.c", Err(c::dcc_exitcode_EXIT_DISTCC_FAILED)),
        (
            "gcc -o /tmp/hello.o -c ../src/hello.c",
            Ok(("../src/hello.c", "/tmp/hello.o")),
        ),
        (
            "gcc -DMYNAME=quasibar.c bar.c -c -o bar.o",
            Ok(("bar.c", "bar.o")),
        ),
        ("gcc -ohello.o -c hello.c", Ok(("hello.c", "hello.o"))),
        ("ccache gcc -c hello.c", Ok(("hello.c", "hello.o"))),
        ("gcc hello.o", Err(100)),
        ("gcc -o hello.o hello.c", Err(100)),
        ("gcc -o hello.o -c hello.s", Err(100)),
        ("gcc -o hello.o -c hello.S", Err(100)),
        ("gcc -fprofile-arcs -ftest-coverage -c hello.c", Err(100)),
        ("gcc -S hello.c", Ok(("hello.c", "hello.s"))),
        ("gcc -c -S hello.c", Ok(("hello.c", "hello.s"))),
        ("gcc -S -c hello.c", Ok(("hello.c", "hello.s"))),
        ("gcc -M hello.c", Err(100)),
        ("gcc -ME hello.c", Err(100)),
        ("gcc -MD -c hello.c", Ok(("hello.c", "hello.o"))),
        ("gcc -MMD -c hello.c", Ok(("hello.c", "hello.o"))),
        // Assemble to stdout (thanks Alexandre).
        ("gcc -S foo.c -o -", Err(100)),
        ("-S -o - foo.c", Err(100)),
        ("-c -S -o - foo.c", Err(100)),
        ("-S -c -o - foo.c", Err(100)),
        // dasho syntax
        ("gcc -ofoo.o foo.c -c", Ok(("foo.c", "foo.o"))),
        ("gcc -ofoo foo.o", Err(100)),
        // tricky this one -- no dashc
        ("foo.c -o foo.o", Err(100)),
        ("foo.c -o foo.o -c", Ok(("foo.c", "foo.o"))),
        // Produce assembly listings
        ("gcc -Wa,-alh,-a=foo.lst -c foo.c", Err(100)),
        ("gcc -Wa,--MD -c foo.c", Err(100)),
        ("gcc -Wa,-xarch=v8 -c foo.c", Ok(("foo.c", "foo.o"))),
        // Produce .rpo files
        ("g++ -frepo foo.C", Err(100)),
        ("gcc -xassembler-with-cpp -c foo.c", Err(100)),
        ("gcc -x assembler-with-cpp -c foo.c", Err(100)),
        ("gcc -specs=foo.specs -c foo.c", Err(100)),
        // Fixed in 2.18.4 -- -dr writes rtl to a local file
        ("gcc -dr -c foo.c", Err(100)),
    ];
    for (args, expected) in cases {
        let args = once("distcc")
            .chain(args.split_whitespace())
            .collect::<Vec<_>>();
        println!("scan_args({:?})", args);
        let r = scan_args(&args);
        println!(" -> {r:?}");
        match r {
            Ok((a, b, _fixed_args)) => {
                // TODO: Also check the expected fixed_args.
                assert_eq!((a.as_str(), b.as_str()), expected.expect("expected Ok"));
            }
            Err(ret) => assert_eq!(ret, expected.unwrap_err()),
        }
    }
}
