# Hybrid Rust + C++20 APU/UMA Core & `metal-gpu-trace` MCP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the zero-allocation Rust `vegaflow-core` static library for APU/UMA memory management, CPU L3/CCX topology partitioning, background confinement hysteresis, and Anti-Cheat signature scanning, link it into the C++20 daemon and test suite via `scripts/build.ps1`, and register/verify the `metal-gpu-trace` MCP server on the laptop.

**Architecture:** A Rust crate (`rust/vegaflow-core`) compiled as `staticlib` and `rlib` for `x86_64-pc-windows-gnu` exposes a `#[repr(C)]` FFI contract (`include/core/rust_bridge.h`) with zero heap allocations on hot paths and direct `ntdll.dll` calls (`NtQuerySystemInformation` / `NtSetSystemInformation`). The existing C++20 subsystems (`MemoryManager`, `HardwareProfiler`, `CpuOrchestrator`, `SandboxGuard`) delegate their core mathematical, topological, and security evaluations to Rust while preserving 100% backward compatibility with the 30 existing tests plus a new Test 31.

**Tech Stack:** Rust 1.98 (`x86_64-pc-windows-gnu`, `panic = "abort"`), C++20 (MinGW-w64 `g++ 16.2.0`), Windows NT Native API (`ntdll.dll`), PowerShell (`scripts/build.ps1`), Python 3.12 (`metal-gpu-trace` MCP + `PresentMon.exe`).

**Spec:** `.claude/docs/ARQUITECTURA/spec-rust-hibrido-uma-mcp.md`

## Global Constraints

- Private Working Set (`MemoryManager::GetCurrentProcessPrivateWorkingSetBytes()`) MUST remain `< 15.0 MB` (`15 * 1024 * 1024` bytes) at all times.
- Zero CPU usage at idle (`0.0%`); no polling threads or busy-wait loops inside `vegaflow-core`.
- All 30 existing tests plus the new Test 31 in `vegaflow_tests.exe` MUST pass (`TEST RESULTS: 31 PASSED, 0 FAILED`), and all Rust unit tests in `cargo test` MUST pass.
- Strict Anti-Cheat safety: never inject DLLs or modify files in anti-cheat protected game directories.
- Registry and power plan state before and after running `vegaflow_tests.exe` MUST remain identical (`scripts/remote/jobs/doctor.ps1` clean).

---

### Task 1: Configure and Verify `metal-gpu-trace` MCP Server & Laptop Control

**Files:**
- Modify: `C:\Users\testuser\.gemini\config\mcp_config.json`

**Interfaces:**
- Consumes: `C:\dev\mgt-venv\Scripts\python.exe`, `C:\dev\metal-gpu-trace\metal_trace_mcp.py`, `C:\ProgramData\VegaFlow\tools\PresentMon.exe`
- Produces: Active `"metal-gpu-trace"` entry in `C:\Users\testuser\.gemini\config\mcp_config.json` verified via JSON-RPC stdio handshake (`initialize` + `tools/list`)

- [ ] **Step 1: Write a verification probe test for `metal-gpu-trace` MCP registration**

Run a PowerShell command that checks whether `"metal-gpu-trace"` is present in `C:\Users\testuser\.gemini\config\mcp_config.json` and whether the configured command responds to an MCP `initialize` and `tools/list` request.

- [ ] **Step 2: Run the verification probe to confirm it fails before configuration**

Expected: Fails because `"metal-gpu-trace"` is not yet in `C:\Users\testuser\.gemini\config\mcp_config.json`.

- [ ] **Step 3: Add `"metal-gpu-trace"` to `C:\Users\testuser\.gemini\config\mcp_config.json`**

Add the following server block inside `"mcpServers"`:
```json
    "metal-gpu-trace": {
      "command": "C:\\dev\\mgt-venv\\Scripts\\python.exe",
      "args": [
        "-I",
        "C:\\dev\\metal-gpu-trace\\metal_trace_mcp.py"
      ],
      "env": {
        "PRESENTMON_EXE": "C:\\ProgramData\\VegaFlow\\tools\\PresentMon.exe",
        "PERF_OUT": "C:\\dev\\perf-runs"
      }
    }
```

- [ ] **Step 4: Run the verification probe and `doctor.ps1` to confirm MCP & laptop health pass**

Verify that sending JSON-RPC `initialize` and `tools/list` to the configured `metal-gpu-trace` command returns the tool definitions (`perf_record`, `perf_attach`, `perf_compare`, etc.) with `isError: false`, and that `scripts/remote/jobs/doctor.ps1` reports clean status.

---

### Task 2: Create Rust Crate `rust/vegaflow-core` & C/C++ FFI Header `include/core/rust_bridge.h` (TDD)

**Files:**
- Create: `include/core/rust_bridge.h`
- Create: `rust/vegaflow-core/Cargo.toml`
- Create: `rust/vegaflow-core/src/lib.rs`
- Create: `rust/vegaflow-core/src/uma_memory.rs`
- Create: `rust/vegaflow-core/src/cpu_governor.rs`
- Create: `rust/vegaflow-core/src/sandbox_core.rs`

**Interfaces:**
- Produces C/C++ header `include/core/rust_bridge.h` and Rust `extern "C"` functions:
  - `VfUmaGuardDecision vf_rust_evaluate_uma_pressure(const VfUmaTelemetryInput* input, uint32_t prev_interventions)`
  - `VfNtMemoryListSample vf_rust_query_nt_memory_lists(void)`
  - `bool vf_rust_purge_nt_standby_list(bool dry_run)`
  - `bool vf_rust_partition_cpu_topology(const VfCpuCoreEntry* entries, uint32_t count, VfTopologyPartitionOut* out)`
  - `bool vf_rust_eval_bg_confinement(double logical_cores_used, bool currently_confined)`
  - `bool vf_rust_has_anticheat_signature(const char* utf8_path, size_t len)`
  - `bool vf_rust_is_critical_system_filename(const char* utf8_name, size_t len)`

- [ ] **Step 1: Ensure `x86_64-pc-windows-gnu` target is installed and scaffold `Cargo.toml` + `rust_bridge.h` + failing TDD tests**

First install `rustup target add x86_64-pc-windows-gnu`.
Write `rust/vegaflow-core/Cargo.toml`:
```toml
[package]
name = "vegaflow-core"
version = "0.1.0"
edition = "2021"

[lib]
name = "vegaflow_core"
crate-type = ["staticlib", "rlib"]

[profile.release]
opt-level = 3
lto = true
codegen-units = 1
panic = "abort"

[profile.dev]
panic = "abort"
```
Write `rust/vegaflow-core/src/lib.rs` with comprehensive `#[cfg(test)] mod tests` testing:
1. `vf_rust_evaluate_uma_pressure`: healthy telemetry (48% DXGI, 4096 MB free -> `guard_triggered == false`), high DXGI pressure (90% DXGI -> `guard_triggered == true`, `total_interventions == prev + 1`), critical low free RAM (1024 MB free < 1536 MB -> `guard_triggered == true`), and discrete GPU pass-through (`is_discrete_tier == true` -> `guard_triggered == false`).
2. `vf_rust_query_nt_memory_lists` and `vf_rust_purge_nt_standby_list(true)`: live NT query returns `valid == true` or graceful fallback, dry-run purge returns `true`.
3. `vf_rust_partition_cpu_topology`:
   - Symmetric 2-CCX Ryzen 5 4600H (6C/12T, 3C/6T per CCX): `background_count == 2` (266, 267), `game_count == 8` with CCX 0 prioritized first (258, 259, 260, 261), `has_multiple_ccx == true`.
   - Asymmetric Zen 4 + Zen 4c (4C/8T on LLC 0, 2C/4T on LLC 1): `background_count == 4` (264..267), `game_count == 6` (258..263).
   - Scaling rule across 4C/4T (`bg=1`), 2C/4T (`bg=1`), 6C/12T (`bg=2`), 8C/16T (`bg=2`), 4C/8T (`bg=2`), 6C/6T (`bg=1`).
4. `vf_rust_eval_bg_confinement`: hysteresis (`0.23` -> `false`, `0.50` -> `true`, `0.35` when confined -> `true`, `0.24` when confined -> `false`).
5. `vf_rust_has_anticheat_signature` & `vf_rust_is_critical_system_filename`: detects `HoYoKProtect.sys`, `EasyAntiCheat_EOS.dll`, `vgk.sys`, `GenshinImpact.exe`, `ZenlessZoneZero.exe`, `pagefile.sys`, `bootmgr` case-insensitively with zero heap allocation, and returns `false` for normal paths (`C:\Games\IndieGame\Engine.ini`).

- [ ] **Step 2: Run `cargo test` to verify RED state**

Run: `cargo test --manifest-path rust/vegaflow-core/Cargo.toml --target x86_64-pc-windows-gnu`
Expected: FAIL on unimplemented stubs.

- [ ] **Step 3: Implement `uma_memory.rs`, `cpu_governor.rs`, and `sandbox_core.rs` with zero heap allocations**

Implement the exact logic in `uma_memory.rs`, `cpu_governor.rs`, and `sandbox_core.rs` using fixed stack arrays (`VF_MAX_LOGICAL_PROCESSORS = 64`), `#[repr(C)]` structs, and `#[link(name = "ntdll")] extern "system"` bindings for `NtQuerySystemInformation` and `NtSetSystemInformation`.

- [ ] **Step 4: Run `cargo test` and `cargo build --release` to verify GREEN state**

Run:
```powershell
cargo test --manifest-path rust/vegaflow-core/Cargo.toml --target x86_64-pc-windows-gnu
cargo build --release --manifest-path rust/vegaflow-core/Cargo.toml --target x86_64-pc-windows-gnu
```
Expected: All Rust unit tests PASS and `rust/vegaflow-core/target/x86_64-pc-windows-gnu/release/libvegaflow_core.a` is generated.

---

### Task 3: Integrate `vegaflow-core` into C++20 Subsystems, `scripts/build.ps1`, and Test 31 (TDD)

**Files:**
- Modify: `scripts/build.ps1`
- Modify: `src/optimizers/memory_manager.cpp`
- Modify: `src/core/hardware_profiler.cpp`
- Modify: `src/optimizers/cpu_orchestrator.cpp`
- Modify: `src/security/sandbox_guard.cpp`
- Modify: `tests/test_main.cpp`

**Interfaces:**
- Consumes: `include/core/rust_bridge.h` and `rust/vegaflow-core/target/x86_64-pc-windows-gnu/release/libvegaflow_core.a`
- Produces: Updated `vegaflow_daemon.exe` and `vegaflow_tests.exe` linked with `libvegaflow_core.a`, passing 31/31 tests (`TEST RESULTS: 31 PASSED, 0 FAILED`)

- [ ] **Step 1: Add Test 31 in `tests/test_main.cpp` and update `scripts/build.ps1` to link `libvegaflow_core.a`**

Add **Test 31: Hybrid Rust FFI Core (Zero-Allocation UMA Guard, L3 Topology, Hysteresis & Anti-Cheat Scanner)** in `tests/test_main.cpp` before the final summary, verifying:
1. Direct FFI calls to `vf_rust_evaluate_uma_pressure`, `vf_rust_query_nt_memory_lists`, `vf_rust_partition_cpu_topology`, `vf_rust_eval_bg_confinement`, `vf_rust_has_anticheat_signature`, and `vf_rust_is_critical_system_filename`.
2. Parity between C++ `HardwareProfiler::PartitionCpuSetsFromTopology` / `MemoryManager::EvaluateUmaPressureGuard` / `SandboxGuard::PathContainsAntiCheatSignature` and the Rust core.
3. Zero-Overhead KPI check after 1,000 hot-loop iterations of the Rust FFI calls (`Private Working Set < 15 MB`).

- [ ] **Step 2: Connect C++20 subsystems to `rust_bridge.h`**

- In `src/optimizers/memory_manager.cpp`:
  - Use `vf_rust_query_nt_memory_lists()` inside `SampleTelemetry()` and `QueryStandbyMemoryMib()`.
  - Use `vf_rust_purge_nt_standby_list(dryRun)` inside `PurgeStandbyList()`.
  - Use `vf_rust_evaluate_uma_pressure(...)` inside `EvaluateUmaPressureGuard()`.
- In `src/core/hardware_profiler.cpp`:
  - Delegate `HardwareProfiler::PartitionCpuSetsFromTopology(CpuTopologyProfile& cpu_topology)` to `vf_rust_partition_cpu_topology(...)` when `all_cpu_sets.size() <= VF_MAX_LOGICAL_PROCESSORS`.
- In `src/optimizers/cpu_orchestrator.cpp`:
  - Delegate the confinement hysteresis check in `UpdateBackgroundLoad` to `vf_rust_eval_bg_confinement(logical_cpus, bg_confine_active_)`.
- In `src/security/sandbox_guard.cpp`:
  - Use `vf_rust_is_critical_system_filename` in `IsBlacklisted` and `vf_rust_has_anticheat_signature` in `PathContainsAntiCheatSignature` (converting `std::wstring` to UTF-8 on stack buffer `char utf8_buf[1024]` via `WideCharToMultiByte` with zero heap allocation).

- [ ] **Step 3: Update `scripts/build.ps1` to build and test `rust/vegaflow-core` and link `libvegaflow_core.a`**

In `scripts/build.ps1`:
- Detect `cargo` in PATH or `$env:USERPROFILE\.cargo\bin\cargo.exe`.
- Run `cargo build --release --manifest-path rust/vegaflow-core/Cargo.toml --target x86_64-pc-windows-gnu`.
- If `-RunTests` is passed, also run `cargo test --manifest-path rust/vegaflow-core/Cargo.toml --target x86_64-pc-windows-gnu`.
- Link `rust/vegaflow-core/target/x86_64-pc-windows-gnu/release/libvegaflow_core.a` (plus `-lws2_32 -luserenv -lbcrypt` if needed by Rust's std on Windows GNU) into both `vegaflow_daemon.exe` and `vegaflow_tests.exe`.

- [ ] **Step 4: Run `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests` and verify `31 PASSED, 0 FAILED`**

Run:
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests
.\build\vegaflow_daemon.exe --status
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\remote\jobs\doctor.ps1
```
Expected:
- Cargo tests pass.
- `TEST RESULTS: 31 PASSED, 0 FAILED`.
- `vegaflow_daemon.exe --status` runs cleanly with Private Working Set `< 15 MB`.
- `doctor.ps1` shows zero out-of-factory tweaks (`--- fuera de fabrica` followed immediately by `fin`).
