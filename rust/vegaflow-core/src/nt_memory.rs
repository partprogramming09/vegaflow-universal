//! Windows memory-list calls through ntdll (an action, not a rule). They move to the C++ side in the
//! memory phase of the master plan; the FFI wrappers are in ffi.rs.

use crate::policy::uma_memory::VfNtMemoryListSample;

#[repr(C)]
struct SystemMemoryListInformation {
    zero_page_count: usize,
    free_page_count: usize,
    modified_page_count: usize,
    modified_no_write_page_count: usize,
    bad_page_count: usize,
    page_count_by_priority: [usize; 8],
    repurposed_pages_by_priority: [usize; 8],
    modified_page_count_page_file: usize,
}

const SYSTEM_MEMORY_LIST_INFORMATION_CLASS: u32 = 80;
const MEMORY_PURGE_STANDBY_LIST_COMMAND: u32 = 4;
const PAGE_SIZE_BYTES: u64 = 4096;

#[cfg(windows)]
#[link(name = "ntdll")]
extern "system" {
    fn NtQuerySystemInformation(
        class: u32,
        info: *mut core::ffi::c_void,
        len: u32,
        ret_len: *mut u32,
    ) -> i32;

    fn NtSetSystemInformation(
        class: u32,
        info: *mut core::ffi::c_void,
        len: u32,
    ) -> i32;
}

// Outside Windows there is no ntdll. These stand-ins return a negative NTSTATUS (failure) so the
// core still builds and its pure logic can be tested on any host; on Windows the real calls above are used.
#[cfg(not(windows))]
#[allow(non_snake_case)]
unsafe fn NtQuerySystemInformation(
    _class: u32,
    _info: *mut core::ffi::c_void,
    _len: u32,
    _ret_len: *mut u32,
) -> i32 {
    -1
}

#[cfg(not(windows))]
#[allow(non_snake_case)]
unsafe fn NtSetSystemInformation(_class: u32, _info: *mut core::ffi::c_void, _len: u32) -> i32 {
    -1
}

pub fn query_memory_lists() -> VfNtMemoryListSample {
    let mut mem_info = core::mem::MaybeUninit::<SystemMemoryListInformation>::uninit();
    let mut return_length: u32 = 0;

    let status = unsafe {
        NtQuerySystemInformation(
            SYSTEM_MEMORY_LIST_INFORMATION_CLASS,
            mem_info.as_mut_ptr() as *mut core::ffi::c_void,
            core::mem::size_of::<SystemMemoryListInformation>() as u32,
            &mut return_length,
        )
    };

    if status >= 0 {
        let info = unsafe { mem_info.assume_init() };
        let free_and_zero =
            (info.zero_page_count as u64 + info.free_page_count as u64) * PAGE_SIZE_BYTES;
        let mut standby: u64 = 0;
        for count in info.page_count_by_priority.iter() {
            standby += *count as u64 * PAGE_SIZE_BYTES;
        }
        let modified = info.modified_page_count as u64 * PAGE_SIZE_BYTES;

        VfNtMemoryListSample {
            free_and_zero_bytes: free_and_zero,
            standby_bytes: standby,
            modified_bytes: modified,
            valid: true,
        }
    } else {
        VfNtMemoryListSample {
            free_and_zero_bytes: 0,
            standby_bytes: 0,
            modified_bytes: 0,
            valid: false,
        }
    }
}

pub fn purge_standby_list(dry_run: bool) -> bool {
    if dry_run {
        return true;
    }

    let mut cmd = MEMORY_PURGE_STANDBY_LIST_COMMAND;
    let status = unsafe {
        NtSetSystemInformation(
            SYSTEM_MEMORY_LIST_INFORMATION_CLASS,
            &mut cmd as *mut u32 as *mut core::ffi::c_void,
            core::mem::size_of::<u32>() as u32,
        )
    };

    status >= 0
}
