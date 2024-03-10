use std::ffi::{c_char, CStr, CString};
use std::iter::once;
use std::ptr::{null, null_mut};

use distcc::c;

#[test]
fn preprocessor_args_stripped_from_command_line() {
    let cases = [(
        "gcc -c hello.c", "gcc -c hello.c"),
        ("cc -Dhello hello.c -c", "cc hello.c -c"),
        ("gcc -g -O2 -W -Wall -Wshadow -Wpointer-arith -Wcast-align -c -o h_strip.o h_strip.c",
        "gcc -g -O2 -W -Wall -Wshadow -Wpointer-arith -Wcast-align -c -o h_strip.o h_strip.c"),
        // invalid but should work
        ("cc -c hello.c -D", "cc -c hello.c"),
        ("cc -c hello.c -D -D", "cc -c hello.c"),
        ("cc -c hello.c -I ../include", "cc -c hello.c"),
        ("cc -c -I ../include hello.c", "cc -c hello.c"),
        ("cc -c -I. -I.. -I../include -I/home/mbp/garnome/include -c -o foo.o foo.c",
        "cc -c -c -o foo.o foo.c"),
        ("cc -c -DDEBUG -DFOO=23 -D BAR -c -o foo.o foo.c",
        "cc -c -c -o foo.o foo.c"),

        // New options stripped in 0.11
        ("cc -o nsinstall.o -c -DOSTYPE=\"Linux2.4\" -DOSARCH=\"Linux\" -DOJI -D_BSD_SOURCE -I../dist/include -I../dist/include -I/home/mbp/work/mozilla/mozilla-1.1/dist/include/nspr -I/usr/X11R6/include -fPIC -I/usr/X11R6/include -Wall -W -Wno-unused -Wpointer-arith -Wcast-align -pedantic -Wno-long-long -pthread -pipe -DDEBUG -D_DEBUG -DDEBUG_mbp -DTRACING -g -I/usr/X11R6/include -include ../config-defs.h -DMOZILLA_CLIENT -Wp,-MD,.deps/nsinstall.pp nsinstall.c",
                "cc -o nsinstall.o -c -fPIC -Wall -W -Wno-unused -Wpointer-arith -Wcast-align -pedantic -Wno-long-long -pthread -pipe -g nsinstall.c"),
    ];
    for (input, expected) in cases {
        println!("input: {:?}", input);
        let input_cstrings = input
            .split(' ')
            .map(|w| CString::new(w).unwrap())
            .collect::<Vec<CString>>();
        let input_ptrs = input_cstrings
            .iter()
            .map(|s| s.as_ptr())
            .chain(once(null()))
            .collect::<Vec<*const c_char>>();
        let mut out_argv = null_mut();
        let ret = unsafe {
            c::dcc_strip_local_args(input_ptrs.as_ptr() as *mut *mut c_char, &mut out_argv)
        };
        assert_eq!(ret, 0);
        let mut out = Vec::new();
        loop {
            let arg = unsafe { *out_argv };
            if arg.is_null() {
                break;
            }
            let s = unsafe { CStr::from_ptr(arg) }.to_str().unwrap();
            out.push(s);
            unsafe { out_argv = out_argv.add(1) };
        }
        assert_eq!(out.join(" "), expected);
    }
}
