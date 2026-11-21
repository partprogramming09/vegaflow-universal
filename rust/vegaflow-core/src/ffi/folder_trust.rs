//! Folder trust: whether only administrators can write a folder, from its DACL.

use super::ffi_guard;
use crate::policy::folder_trust::{self, Ace, SidKind};

/// Who a SID is, for `vf_rust_only_admins_can_write`; any other number counts as another account.
pub const VF_SID_OTHER: u8 = 0;
pub const VF_SID_SYSTEM: u8 = 1;
pub const VF_SID_ADMINISTRATORS: u8 = 2;
pub const VF_SID_TRUSTED_INSTALLER: u8 = 3;

/// One DACL entry, as C++ reads it.
#[repr(C)]
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct VfAceEntry {
    pub mask: u32,
    pub sid_kind: u8,
    pub allows: bool,
    pub inherit_only: bool,
}

fn sid_kind(k: u8) -> SidKind {
    match k {
        VF_SID_SYSTEM => SidKind::System,
        VF_SID_ADMINISTRATORS => SidKind::Administrators,
        VF_SID_TRUSTED_INSTALLER => SidKind::TrustedInstaller,
        _ => SidKind::Other,
    }
}

/// See `folder_trust::only_admins_can_write`. False for bad pointers or a panic.
#[no_mangle]
pub extern "C" fn vf_rust_only_admins_can_write(has_dacl: bool, owner_kind: u8, aces: *const VfAceEntry, count: usize) -> bool {
    ffi_guard(false, || {
        if aces.is_null() && count != 0 {
            return false;
        }
        let entries = if count == 0 { &[][..] } else { unsafe { core::slice::from_raw_parts(aces, count) } };
        let aces: Vec<Ace> = entries
            .iter()
            .map(|e| Ace { mask: e.mask, sid: sid_kind(e.sid_kind), allows: e.allows, inherit_only: e.inherit_only })
            .collect();
        folder_trust::only_admins_can_write(has_dacl, sid_kind(owner_kind), &aces)
    })
}
