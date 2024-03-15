//! Manage the list of hosts to distribute compilation to.

#![allow(clippy::missing_safety_doc)] // just for now

use crate::c;
use crate::glue::cstr_to_owned;

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct HostDef {
    pub mode: ConnectionMode,
    // `is_up` from C is not here; we'll track state rather than definition elsewhere.
    pub n_slots: usize,
    pub compression: Compression,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub enum ConnectionMode {
    Local,
    Tcp {
        hostname: String,
        port: u16,
    },
    Ssh {
        user: Option<String>,
        hostname: String,
        command: Option<String>,
    },
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub enum Compression {
    Uncompressed,
    Lzo1X,
}

impl HostDef {
    pub unsafe fn from_c(from: *const c::dcc_hostdef) -> HostDef {
        unsafe {
            let from = &*from;
            let mode = match from.mode {
                c::dcc_hostdef_DCC_MODE_LOCAL => ConnectionMode::Local,
                c::dcc_hostdef_DCC_MODE_TCP => ConnectionMode::Tcp {
                    hostname: cstr_to_owned(from.hostname).expect("hostname"),
                    port: from.port.try_into().expect("convert port"),
                },
                c::dcc_hostdef_DCC_MODE_SSH => ConnectionMode::Ssh {
                    user: cstr_to_owned(from.user),
                    hostname: cstr_to_owned(from.hostname).expect("hostname"),
                    command: cstr_to_owned(from.ssh_command),
                },
                _ => panic!("Unknown connection mode {}", from.mode),
            };
            let compression = match from.compr {
                c::dcc_compress_DCC_COMPRESS_NONE => Compression::Uncompressed,
                c::dcc_compress_DCC_COMPRESS_LZO1X => Compression::Lzo1X,
                x => panic!("Unknown compression mode {x}"),
            };
            HostDef {
                mode,
                compression,
                n_slots: from.n_slots.try_into().expect("convert n_slots"),
            }
        }
    }
}
