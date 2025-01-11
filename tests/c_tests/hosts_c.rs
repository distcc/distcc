//! Test host list parsing.

use std::ffi::CString;
use std::ptr::null_mut;

use test_log::test;

use distcc::c;
use distcc::glue::trace::route_c_trace_to_rust;
use distcc::hostlist::HostDef;

fn parse_host_list(hosts: &str) -> Result<Vec<HostDef>, i32> {
    route_c_trace_to_rust();
    let mut host_list = Vec::new();
    let hosts_c = CString::new(hosts).unwrap();
    let mut n_hosts = 0;
    unsafe {
        let mut hostdefs = null_mut();
        let ret = c::dcc_parse_hosts(
            hosts_c.as_ptr(),
            c"example".as_ptr(),
            &mut hostdefs,
            &mut n_hosts,
            null_mut(),
        );
        if ret != 0 {
            return Err(ret);
        }
        let mut host = hostdefs;
        while !host.is_null() {
            // TODO: Extract other fields
            host_list.push(HostDef::from_c(host));
            let next_host = (*host).next;
            c::dcc_free_hostdef(host);
            host = next_host;
        }
        // TODO: Free the allocated list
    }
    assert_eq!(n_hosts, host_list.len() as i32);
    Ok(host_list)
}

#[test]
fn invalid_host_lists() {
    // This generates some spam on stderr, but the C code should be replaced soon...
    for &hosts in &[
        "", " ", " , ", " , , ", "    ", "\t", "  @ ", ":", "mbp@", "angry::", ":4200",
    ] {
        assert_eq!(
            parse_host_list(hosts),
            Err(c::dcc_exitcode_EXIT_BAD_HOSTSPEC as i32)
        );
    }
}

#[test]
fn valid_host_list() {
    let example = "
        localhost 127.0.0.1 @angry   ted@angry
        \t@angry:/home/mbp/bin/distccd  angry:4204
        ipv4-localhost
        angry/44
        angry:300/44
        angry/44:300
        angry,lzo
        angry:3000,lzo    # some comment
        angry/44,lzo
        @angry,lzo#asdasd
        # oh yeah nothing here
        @angry:/usr/sbin/distccd,lzo
        localhostbutnotreally
        ";
    let hosts = parse_host_list(example).unwrap();
    dbg!(&hosts);
    use distcc::hostlist::Compression::*;
    use distcc::hostlist::ConnectionMode::*;
    assert_eq!(
        hosts,
        [
            HostDef {
                mode: Local,
                n_slots: 2,
                compression: Uncompressed,
            },
            HostDef {
                mode: Tcp {
                    hostname: "127.0.0.1".into(),
                    port: 3632,
                },
                n_slots: 4,
                compression: Uncompressed,
            },
            HostDef {
                mode: Ssh {
                    user: None,
                    hostname: "angry".into(),
                    command: None,
                },
                n_slots: 4,
                compression: Uncompressed,
            },
            HostDef {
                mode: Ssh {
                    user: Some("ted".into()),
                    hostname: "angry".into(),
                    command: None,
                },
                n_slots: 4,
                compression: Uncompressed,
            },
            HostDef {
                mode: Ssh {
                    user: None,
                    hostname: "angry".into(),
                    command: Some("/home/mbp/bin/distccd".into()),
                },
                n_slots: 4,
                compression: Uncompressed,
            },
            HostDef {
                mode: Tcp {
                    hostname: "angry".into(),
                    port: 4204,
                },
                n_slots: 4,
                compression: Uncompressed,
            },
            HostDef {
                mode: Tcp {
                    hostname: "ipv4-localhost".into(),
                    port: 3632,
                },
                n_slots: 4,
                compression: Uncompressed,
            },
            HostDef {
                mode: Tcp {
                    hostname: "angry".into(),
                    port: 3632,
                },
                n_slots: 44,
                compression: Uncompressed,
            },
            HostDef {
                mode: Tcp {
                    hostname: "angry".into(),
                    port: 300,
                },
                n_slots: 44,
                compression: Uncompressed,
            },
            HostDef {
                mode: Tcp {
                    hostname: "angry".into(),
                    port: 300,
                },
                n_slots: 44,
                compression: Uncompressed,
            },
            HostDef {
                mode: Tcp {
                    hostname: "angry".into(),
                    port: 3632,
                },
                n_slots: 4,
                compression: Lzo1X,
            },
            HostDef {
                mode: Tcp {
                    hostname: "angry".into(),
                    port: 3000,
                },
                n_slots: 4,
                compression: Lzo1X,
            },
            HostDef {
                mode: Tcp {
                    hostname: "angry".into(),
                    port: 3632,
                },
                n_slots: 44,
                compression: Lzo1X,
            },
            HostDef {
                mode: Ssh {
                    user: None,
                    hostname: "angry".into(),
                    command: None,
                },
                n_slots: 4,
                compression: Lzo1X,
            },
            HostDef {
                mode: Ssh {
                    user: None,
                    hostname: "angry".into(),
                    command: Some("/usr/sbin/distccd".into(),),
                },
                n_slots: 4,
                compression: Lzo1X,
            },
            HostDef {
                mode: Tcp {
                    hostname: "localhostbutnotreally".into(),
                    port: 3632,
                },
                n_slots: 4,
                compression: Uncompressed,
            },
        ]
    );
}
