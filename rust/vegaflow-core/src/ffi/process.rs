//! Processes: classify a running process and check a game name.

use super::config::VfProcessExceptionEntry;
use super::{bytes_or_empty, ffi_guard};
use crate::policy::config_rules;
use crate::policy::process_class::{self, ProcessPolicyInput, VfProcessCategoryActions, VfProcessDecision};
use crate::policy::process_tree::{self, ProcessNode};

#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct VfProcessClassifyInput {
    pub full_path_ptr: *const u8,
    pub full_path_len: usize,
    pub exe_name_ptr: *const u8,
    pub exe_name_len: usize,
    pub category_actions: VfProcessCategoryActions,
    pub exceptions_ptr: *const VfProcessExceptionEntry,
    pub exceptions_count: usize,
    pub custom_games_nul_ptr: *const u8,
    pub custom_games_nul_len: usize,
    pub excluded_games_nul_ptr: *const u8,
    pub excluded_games_nul_len: usize,
    pub heavy_work_as_game: bool,
    pub is_foreground_3d: bool,
    pub has_anticheat_signal: bool,
}

fn split_nul_utf8(buf: &[u8]) -> Vec<&str> {
    if buf.is_empty() {
        return Vec::new();
    }
    let body = buf.strip_suffix(&[0]).unwrap_or(buf);
    body.split(|&b| b == 0)
        .filter(|s| !s.is_empty())
        .filter_map(|s| std::str::from_utf8(s).ok())
        .collect()
}

#[no_mangle]
pub extern "C" fn vf_rust_classify_process(input: *const VfProcessClassifyInput, out: *mut VfProcessDecision) -> bool {
    ffi_guard(false, || {
        if input.is_null() || out.is_null() {
            return false;
        }
        let inp = unsafe { &*input };
        let Some(fp_bytes) = (unsafe { bytes_or_empty(inp.full_path_ptr, inp.full_path_len) }) else {
            return false;
        };
        let Some(exe_bytes) = (unsafe { bytes_or_empty(inp.exe_name_ptr, inp.exe_name_len) }) else {
            return false;
        };
        let Some(custom_bytes) = (unsafe { bytes_or_empty(inp.custom_games_nul_ptr, inp.custom_games_nul_len) }) else {
            return false;
        };
        let Some(excl_bytes) = (unsafe { bytes_or_empty(inp.excluded_games_nul_ptr, inp.excluded_games_nul_len) }) else {
            return false;
        };
        if inp.exceptions_ptr.is_null() && inp.exceptions_count != 0 {
            return false;
        }
        let full_path = std::str::from_utf8(fp_bytes).unwrap_or("");
        let exe_name = std::str::from_utf8(exe_bytes).unwrap_or("");
        let custom_games = split_nul_utf8(custom_bytes);
        let excluded_games = split_nul_utf8(excl_bytes);
        let mut exceptions: Vec<(&str, u8)> = Vec::with_capacity(inp.exceptions_count);
        if inp.exceptions_count > 0 {
            let ex_slice = unsafe { core::slice::from_raw_parts(inp.exceptions_ptr, inp.exceptions_count) };
            for e in ex_slice {
                if let Some(eb) = unsafe { bytes_or_empty(e.exe_ptr, e.exe_len) } {
                    if let Ok(es) = std::str::from_utf8(eb) {
                        exceptions.push((es, e.action));
                    }
                }
            }
        }
        let dec = process_class::classify_process(&ProcessPolicyInput {
            full_path,
            exe_name,
            category_actions: inp.category_actions,
            exceptions: &exceptions,
            custom_game_exes: &custom_games,
            excluded_game_exes: &excluded_games,
            heavy_work_as_game: inp.heavy_work_as_game,
            is_foreground_3d: inp.is_foreground_3d,
            has_anticheat_signal: inp.has_anticheat_signal,
        });
        unsafe { *out = dec };
        true
    })
}

/// See `config_rules::is_allowed_game_exe`, for a name in UTF-8. False for invalid UTF-8, bad pointers or a panic.
#[no_mangle]
pub extern "C" fn vf_rust_is_allowed_game_exe(utf8_name: *const u8, len: usize) -> bool {
    ffi_guard(false, || {
        if utf8_name.is_null() {
            return false;
        }
        let bytes = unsafe { core::slice::from_raw_parts(utf8_name, len) };
        std::str::from_utf8(bytes).is_ok_and(config_rules::is_allowed_game_exe)
    })
}

/// One process of a snapshot for `vf_rust_inherit_process_actions`: its link to the parent and its own decision.
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct VfProcessTreeNode {
    pub pid: u32,
    pub parent_pid: u32,
    pub creation_time: u64,
    pub process_class: u8,
    pub action: u8,
    pub has_user_exception: bool,
    pub has_anticheat_signal: bool,
}

/// F5.1: writes `count` decisions to `out`, one per node in the same order: an unknown child takes the
/// Throttle/EcoOnly action of its nearest proven ancestor (see `policy/process_tree.rs`). On false `out` is untouched.
#[no_mangle]
pub extern "C" fn vf_rust_inherit_process_actions(
    nodes: *const VfProcessTreeNode,
    count: usize,
    active_game_pid: u32,
    out: *mut VfProcessDecision,
) -> bool {
    ffi_guard(false, || {
        if count == 0 {
            return true;
        }
        if nodes.is_null() || out.is_null() {
            return false;
        }
        let raw = unsafe { core::slice::from_raw_parts(nodes, count) };
        let list: Vec<ProcessNode> = raw
            .iter()
            .map(|n| ProcessNode {
                pid: n.pid,
                parent_pid: n.parent_pid,
                creation_time: n.creation_time,
                decision: VfProcessDecision {
                    process_class: n.process_class,
                    action: n.action,
                    protected_by_anticheat: false,
                    path_verified: false,
                },
                has_user_exception: n.has_user_exception,
                has_anticheat_signal: n.has_anticheat_signal,
            })
            .collect();
        let decided = process_tree::inherit_decisions(&list, active_game_pid);
        let dst = unsafe { core::slice::from_raw_parts_mut(out, count) };
        dst.copy_from_slice(&decided);
        true
    })
}
