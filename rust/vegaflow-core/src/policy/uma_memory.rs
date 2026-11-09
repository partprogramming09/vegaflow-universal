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

pub fn evaluate_pressure(telem: &VfUmaTelemetryInput, prev_interventions: u32) -> VfUmaGuardDecision {
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
