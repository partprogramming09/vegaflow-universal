//! Whether only SYSTEM, Administrators and TrustedInstaller can change a file or folder, from its owner and DACL.
//! The daemon runs as administrator, so it only trusts `config.json` when no other account could have written it.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SidKind {
    Other,
    System,
    Administrators,
    TrustedInstaller,
}

/// One entry of the DACL, as the C++ side reads it.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Ace {
    pub mask: u32,
    pub sid: SidKind,
    /// Grants access (any allow type). Deny entries only take rights away.
    pub allows: bool,
    /// Applies only to children, not to the object itself.
    pub inherit_only: bool,
}

/// Rights that change a file or folder's content, name, owner or permissions (file, generic and standard rights).
pub const WRITE_RIGHTS: u32 = FILE_WRITE_DATA
    | FILE_APPEND_DATA
    | FILE_WRITE_EA
    | FILE_DELETE_CHILD
    | FILE_WRITE_ATTRIBUTES
    | DELETE
    | WRITE_DAC
    | WRITE_OWNER
    | MAXIMUM_ALLOWED
    | GENERIC_ALL
    | GENERIC_WRITE;
const FILE_WRITE_DATA: u32 = 0x2;
const FILE_APPEND_DATA: u32 = 0x4;
const FILE_WRITE_EA: u32 = 0x10;
const FILE_DELETE_CHILD: u32 = 0x40;
const FILE_WRITE_ATTRIBUTES: u32 = 0x100;
const DELETE: u32 = 0x0001_0000;
const WRITE_DAC: u32 = 0x0004_0000;
const WRITE_OWNER: u32 = 0x0008_0000;
const MAXIMUM_ALLOWED: u32 = 0x0200_0000;
const GENERIC_ALL: u32 = 0x1000_0000;
const GENERIC_WRITE: u32 = 0x4000_0000;

/// `has_dacl` false means a NULL DACL, which grants everything to everyone.
pub fn only_admins_can_write(has_dacl: bool, owner: SidKind, aces: &[Ace]) -> bool {
    let is_admin = |sid: SidKind| sid != SidKind::Other;
    let others_can_write =
        aces.iter().any(|a| a.allows && !a.inherit_only && !is_admin(a.sid) && a.mask & WRITE_RIGHTS != 0);
    has_dacl && is_admin(owner) && !others_can_write
}

#[cfg(test)]
mod tests {
    use super::*;

    fn allow(sid: SidKind, mask: u32) -> Ace {
        Ace { mask, sid, allows: true, inherit_only: false }
    }

    const FULL: u32 = 0x001F_01FF;
    const READ_EXECUTE: u32 = 0x0012_00A9;

    #[test]
    fn the_installed_folder_is_trusted() {
        let aces = [allow(SidKind::System, FULL), allow(SidKind::Administrators, FULL), allow(SidKind::Other, READ_EXECUTE)];
        assert!(only_admins_can_write(true, SidKind::Administrators, &aces));
        assert!(only_admins_can_write(true, SidKind::System, &aces));
        assert!(only_admins_can_write(true, SidKind::TrustedInstaller, &[allow(SidKind::TrustedInstaller, FULL)]));
    }

    #[test]
    fn any_write_right_for_another_account_is_not_trusted() {
        for bit in [0x2u32, 0x4, 0x10, 0x40, 0x100, 0x0001_0000, 0x0004_0000, 0x0008_0000, 0x0200_0000, 0x1000_0000, 0x4000_0000] {
            let aces = [allow(SidKind::Administrators, FULL), allow(SidKind::Other, READ_EXECUTE | bit)];
            assert!(!only_admins_can_write(true, SidKind::Administrators, &aces), "right {bit:#x}");
        }
    }

    #[test]
    fn an_owner_that_is_not_an_administrator_is_not_trusted() {
        // The owner can always rewrite the DACL, even without an entry for it.
        assert!(!only_admins_can_write(true, SidKind::Other, &[allow(SidKind::Administrators, FULL)]));
    }

    #[test]
    fn a_null_dacl_is_not_trusted() {
        assert!(!only_admins_can_write(false, SidKind::Administrators, &[]));
    }

    #[test]
    fn an_empty_dacl_is_trusted() {
        assert!(only_admins_can_write(true, SidKind::Administrators, &[]));
    }

    #[test]
    fn deny_entries_and_inherit_only_entries_do_not_count() {
        let deny = Ace { mask: FULL, sid: SidKind::Other, allows: false, inherit_only: false };
        let children_only = Ace { mask: FULL, sid: SidKind::Other, allows: true, inherit_only: true };
        assert!(only_admins_can_write(true, SidKind::Administrators, &[deny, children_only]));
    }
}
