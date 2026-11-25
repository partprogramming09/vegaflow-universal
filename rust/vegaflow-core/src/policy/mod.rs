//! The rules: pure decisions over plain data, one per file. No pointers, no FFI, no system calls;
//! the exported C functions that call them are in `ffi/`.

pub mod apu_tier;
pub mod config_file;
pub mod config_rules;
pub mod cpu_governor;
pub mod device_profile;
pub mod epic_manifest;
pub mod file_access;
pub mod folder_trust;
pub mod gog_registry;
pub mod ini_merge;
pub mod install_dir;
pub mod json_text;
pub mod memory_purge;
pub mod path_policy;
pub mod process_class;
pub mod process_tree;
pub mod steam_library;
pub mod sandbox_core;

// E2.5
pub mod doctor;

// E2.2
pub mod memory_compression;
