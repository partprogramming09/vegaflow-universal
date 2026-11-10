//! The rules: pure decisions over plain data, one per file. No pointers, no FFI, no system calls;
//! the exported C functions that call them are in `ffi.rs`.

pub mod apu_tier;
pub mod config_file;
pub mod config_rules;
pub mod cpu_governor;
pub mod file_access;
pub mod folder_trust;
pub mod json_text;
pub mod path_policy;
pub mod steam_library;
pub mod sandbox_core;
pub mod uma_memory;
