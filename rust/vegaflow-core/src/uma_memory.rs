#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VfUmaTelemetryInput {
    pub total_physical_bytes: u64,
    pub free_and_zero_bytes: u64,
    pub standby_bytes: u64,
    pub local_budget_bytes: u64,
    pub local_usage_bytes: u64,
    pub runtime_pressure_threshold: f64,
    pub vram_valid: bool,
    pub is_discrete_tier: bool,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct VfUmaGuardDecision {
    pub available_os_headroom_mb: u64,
    pub dxgi_budget_utilization: f64,
    pub guard_triggered: bool,
    pub dxgi_over_ceiling: bool,
    pub free_ram_critical: bool,
    pub total_interventions: u32,
}

#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct VfNtMemoryListSample {
    pub free_and_zero_bytes: u64,
    pub standby_bytes: u64,
    pub modified_bytes: u64,
    pub valid: bool,
}

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

#[no_mangle]
pub extern "C" fn vf_rust_evaluate_uma_pressure(
    input: *const VfUmaTelemetryInput,
    prev_interventions: u32,
) -> VfUmaGuardDecision {
    if input.is_null() {
        return VfUmaGuardDecision {
            available_os_headroom_mb: 0,
            dxgi_budget_utilization: 0.0,
            guard_triggered: false,
            dxgi_over_ceiling: false,
            free_ram_critical: false,
            total_interventions: prev_interventions,
        };
    }

    let telem = unsafe { &*input };
    let available_os_headroom_mb =
        telem.free_and_zero_bytes.saturating_add(telem.standby_bytes) / (1024 * 1024);

    let dxgi_budget_utilization = if telem.vram_valid && telem.local_budget_bytes > 0 {
        (telem.local_usage_bytes as f64) / (telem.local_budget_bytes as f64)
    } else {
        0.0
    };

    let threshold = if telem.runtime_pressure_threshold > 0.0 {
        telem.runtime_pressure_threshold
    } else {
        0.85
    };

    let dxgi_over_ceiling = telem.vram_valid
        && telem.local_budget_bytes > 0
        && dxgi_budget_utilization >= threshold;

    let free_ram_critical =
        (telem.total_physical_bytes > 0) && ((telem.free_and_zero_bytes / (1024 * 1024)) < 1536);

    let guard_triggered = if telem.is_discrete_tier {
        false
    } else {
        dxgi_over_ceiling || free_ram_critical
    };

    let total_interventions = if guard_triggered {
        prev_interventions.saturating_add(1)
    } else {
        prev_interventions
    };

    VfUmaGuardDecision {
        available_os_headroom_mb,
        dxgi_budget_utilization,
        guard_triggered,
        dxgi_over_ceiling,
        free_ram_critical,
        total_interventions,
    }
}

#[no_mangle]
pub extern "C" fn vf_rust_query_nt_memory_lists() -> VfNtMemoryListSample {
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

#[no_mangle]
pub extern "C" fn vf_rust_purge_nt_standby_list(dry_run: bool) -> bool {
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
