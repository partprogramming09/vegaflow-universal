# Modules: repository tree

**In short:** this page is a map of the repository, folder by folder, from the build scripts to the tests. Use it to find where a file lives. For example, `scripts/ci/check_layers.sh` checks that each folder of `src/` uses only the layers allowed for it, and it runs in the CI. The meaning of the layers is explained in [Code layers](./layers.md).

**Reader:** people

## 4. Repository structure and code modules

```
vegaflow-universal/
├── .gitignore                    # Git exclusion rules (artifacts, temporary files, logs)
├── CHANGELOG.md                  # Version history (version 0.1.0 is the first)
├── config/
│   └── config.json               # Persistent daemon configuration and game list
├── scripts/
│   ├── build/
│   │   ├── build.ps1             # Automated PowerShell build and test script
│   │   ├── build-cross.sh        # The same, from a Mac or Linux
│   │   └── package.sh            # Builds dist/VegaFlow-<version>-win-x64.zip
│   ├── ci/
│   │   └── check_layers.sh       # Checks the layers of src/ (runs in CI)
│   ├── *.ps1                     # Measurement and Windows-state scripts (flat on purpose, see scripts/README.md)
│   └── package/                  # What goes inside the zip: install.ps1 and uninstall.ps1 (see the install guide)
├── rust/
│   └── vegaflow-core/            # High-security Rust hybrid core and zero-allocation FFI
│       ├── Cargo.toml            # staticlib/rlib with LTO and panic = unwind (ffi_guard catches the panics)
│       └── src/
│           ├── lib.rs            # Declares the modules and the tests of the FFI boundary
│           ├── ffi/              # All the vf_rust_* functions exported to C++, one file per domain (mod.rs holds ffi_guard)
│           └── policy/           # The rules, one per file, with no pointers or system calls
│               ├── apu_tier.rs     # APU tier (1 to 4)
│               ├── config_file.rs  # Read and save config.json without losing unknown keys
│               ├── config_rules.rs # config.json ranges and allowed game names
│               ├── cpu_governor.rs # CPU Sets within the process group (L3/CCX, hybrid with LP E-core exclusion) and the confinement hysteresis
│               ├── device_profile.rs # Universal machine profile: GPU (PCI ID/DXCore/pref), power, ARM64 and cautious flags
│               ├── doctor.rs       # --doctor rules: free disk for the shader cache, cache state, and OEM tools that fight VegaFlow
│               ├── file_access.rs  # Full SandboxGuard decision: blacklist, local path, anti-cheat and allowed folders
│               ├── folder_trust.rs # Can only administrators write this folder? (owner and DACL)
│               ├── ini_merge.rs    # Atomic editor for .ini (Unreal) and boot.config (Unity), preserving BOM/UTF-16/CRLF
│               ├── json_text.rs    # Strict JSON reader and writer, no dependencies
│               ├── memory_compression.rs # Should Windows memory compression be turned off? Not at 16 GB of RAM or less
│               ├── memory_purge.rs # Single purge rule and UMA guard (85% threshold, 90s pause, disk, and the factory "No tocar" ("do not touch") list)
│               ├── path_policy.rs  # SandboxGuard path blacklist
│               ├── process_class.rs # Process classification and action (OBS/Discord, "No tocar", verified path, exceptions and AC)
│               ├── process_tree.rs  # A child of a throttled program takes its action; cut at the game, Windows, protected processes and unprovable links (F5.1)
│               ├── steam_library.rs # libraryfolders.vdf reader (Steam libraries)
│               └── sandbox_core.rs # Anti-cheat signatures and critical system files
├── src/                          # Each .hpp lives next to its .cpp, grouped by function
│   ├── app/                      # Program entry points (main.cpp only orchestrates)
│   │   ├── main.cpp              # Entry point: reads the command line and picks the mode
│   │   ├── cli.*                 # One-shot flags and commands (--status, --purge-now, --version, ...)
│   │   ├── daemon_loop.*         # Default mode: the daemon loop and clean shutdown
│   │   ├── gui_mode.*            # --gui: native window with tray; assembles `session/` and connects the buttons (see 3.3b)
│   │   ├── monitor.*             # --monitor: live console panel
│   │   ├── doctor_probe.*        # Windows adapter of the `DoctorProbe` port (`WindowsDoctorProbe`; the port is `ports/doctor_probe.hpp`): disks, caches, hardware, config, processes
│   │   ├── app_state.*           # Shared state and the Ctrl+C handler
│   │   ├── run_cycle.*           # One evaluation cycle: detect a game, apply or release (builds the real Windows classes)
│   │   ├── report_commands.*     # --status, --dry-run and --purge-now (build the real detectors and optimizers)
│   │   └── system_reads.hpp      # Windows adapter of the `SystemReads` port (`WindowsSystemReads`)
│   ├── session/                  # Use cases: what the daemon does with a game (includes nothing from detect/, optimize/ or windows/)
│   │   ├── game_session.*        # Session injectable through ports, GPU hot-plug detection, static/live step and on-the-fly exclusion
│   │   ├── reports.*             # The session log and the report lines (--status, --monitor)
│   │   ├── system_info.*         # SystemInfo report (hardware, UMA, RAM, OS and JSON); the Windows readings are requested from windows/system_counters
│   │   ├── config.*              # ConfigStore: read and save config.json (rules in Rust) and the game list
│   │   ├── restore.*             # Startup evaluation, journal recovery, history format, restore point and `RestoreAllMessage`
│   │   ├── startup_prompts.*     # Who may be asked at startup (only a visible console) and the safe default when nobody answers
│   │   ├── *_screen.*            # Pure models of the screens (home, settings, games, history, config, programs, equipment, requirements)
│   │   ├── settings_catalog.*    # Catalog of settings for the System settings screen (families by hardware, experimental, evidence)
│   │   ├── memory_screen.*       # Model of the Memory tab (F4.2): CPU sets, memory numbers, "no data" states, keyboard logic
│   │   ├── config_recovery.*     # F4.4: what happens to a broken config.json; the person decides, nothing is overwritten by itself
│   │   ├── sidebar_nav.*         # Keyboard logic of the side menu: active entry, arrows, Ctrl+digit
│   │   ├── gui_cadence.*         # How often the window polls (1.5 s visible; 30 s safety net when hidden with no game)
│   │   ├── profile_reasons.*     # Plain sentences that say why a hardware profile is what it is (the words live in Rust)
│   │   └── *_actions.*           # What the buttons do: `GameActions`, `SettingsActions` and `ConfigActions`
│   ├── ports/                    # Interfaces that the use cases ask for and the tests fake
│   │   ├── system_reads.hpp      # What SystemInfo reads (hardware, memory sample, tweaks, L3, DIMM slots)
│   │   ├── tweak_controls.hpp    # The Ajustes switches (implemented by `GraphicsOptimizer`)
│   │   ├── config_location.hpp   # Folder of the .exe and path of config.json (implemented in windows/config_location.cpp)
│   │   ├── file_system.hpp       # Read, write, replace whole, copy and delete files
│   │   ├── clock.hpp             # The current time and readable date (`WallNow`); the tests move it by hand
│   │   ├── device_probe.hpp      # Machine probe (GPU/DXCore/SetupAPI, per-game disk, power, displays, ARM64)
│   │   ├── memory_probe.hpp      # Memory telemetry (NT lists without cache backing, DXGI VRAM budget and disk reading)
│   │   ├── memory_control.hpp    # Single purge action (trim of working sets and purge of the Standby List)
│   │   ├── process_control.hpp   # Process control checking PID + creation_time (EcoOnly vs Throttle, and games)
│   │   ├── registry.hpp          # Read, write, delete and backup in the Windows registry
│   │   ├── power.hpp             # Read/write of the active power plan and AC/DC restore
│   │   ├── change_journal.hpp    # Atomic change journal and the system's undo port
│   │   ├── doctor_probe.hpp      # The reads of `--doctor` as plain data (the real one is `app/doctor_probe.cpp`)
│   │   ├── game_files.hpp        # What the Juegos buttons change in a game folder (the real one is `optimize/game_files_adapter.hpp`)
│   │   └── text_convert.hpp      # UTF-8 <-> UTF-16 (`Utf8ToWide`, `WideToUtf8`); `windows/text_convert.cpp` implements it
│   ├── model/                    # Shared types, no logic
│   │   ├── hardware.hpp          # GPU (connection, PCI IDs, driver), CPU, disk, power, Tier and policy
│   │   ├── game.hpp              # Detected game, graphics API and process classification/policy
│   │   ├── console.hpp           # `StdinKind`: where stdin comes from (visible or hidden console, pipe, file)
│   │   ├── memory.hpp, steam.hpp, tweaks.hpp # Plain data of the optimizers (thresholds, reports, Steam games, applied tweaks)
│   │   ├── hardware_names.hpp    # Plain names of the hardware enums
│   │   ├── engine_presets.*      # The Unreal block and Unity keys a preset writes
│   │   ├── path_name.hpp         # File-name part of a Windows path
│   │   └── doctor.hpp            # Disk and cache rows of `--doctor`
│   ├── detect/                   # What hardware and which game are present
│   │   ├── hardware_profiler.*   # Orchestration of the universal probe and policy from Rust (`device_profile.rs`)
│   │   └── process_detector.*    # Game detection (path + 3D DLL + generic AC signals) and classification via Rust
│   ├── optimize/                 # The optimizations
│   │   ├── graphics/             # 6 injectable families behind `RegistryPort`/`PowerPort` (no static methods)
│   │   │   ├── graphics_optimizer.hpp   # Public API (class `GraphicsOptimizer` + 6 families)
│   │   │   ├── dxgi_flip.cpp            # `DxgiFlipTweaks`: Flip Model and DirectFlip per game
│   │   │   ├── tweaks_query.cpp         # Reads the state of the settings via `RegistryPort`/`PowerPort`
│   │   │   ├── kernel_tweaks.cpp        # `KernelSchedulerTweaks`: priority, MMCSS base and paging shield
│   │   │   ├── display_tweaks.cpp       # `DisplayTweaks`: MPO (off by default), GameDVR and FSO
│   │   │   ├── mmcss.cpp                # `MmcssTweaks`: MMCSS profile for games (without `GPU Priority`)
│   │   │   ├── amd_driver_tweaks.cpp    # `AmdDriverTweaks`: tessellation and boost of the AMD driver (`ven_1002`)
│   │   │   ├── power_mode.cpp           # `PowerModeTweaks`: power plan with AC/DC restore
│   │   │   ├── vram_reporting.cpp       # Part of `AmdDriverTweaks`: reporting the UMA VRAM
│   │   │   ├── shader_cache.cpp         # Part of `AmdDriverTweaks`: shader cache
│   │   │   └── graphics_internal.hpp    # Backup and helpers shared over `RegistryPort` (not public API)
│   │   ├── steam/                # Steam library and presets per engine
│   │   │   ├── steam_engine_optimizer.hpp  # Public API
│   │   │   ├── steam_library.cpp       # Steam installation and game scan
│   │   │   ├── engine_detection.cpp    # Which engine each game uses
│   │   │   ├── engine_presets.cpp      # Apply and restore the UE/Unity presets
│   │   │   ├── apu_shadow_config.cpp   # "Shadow" configuration for APUs
│   │   │   └── steam_internal.*        # Shared helpers
│   │   ├── memory_manager.*      # Memory orchestration via MemoryProbe/MemoryControl and memory_purge.rs
│   │   ├── cpu_orchestrator.*    # CPU Set assignment, restore when the game changes, and safe isolation (PID + FILETIME)
│   │   └── dxvk_manager.*        # Safe deployment of DXVK-GPLAsync, and backups
│   ├── ui/                       # Native Win32/GDI window
│   │   ├── gui_window.*          # Slate/Zinc interface and System Tray
│   │   ├── app_identity.hpp      # Version, window class and title, tray id and the "processor | graphics" line (names only, no Windows calls)
│   │   ├── gui_components.*      # Modular UI components and graphics rendering
│   │   ├── native_controls.*     # Shared parts of the native controls: create, place with scrolling, frame and focus ring
│   │   ├── texts.hpp             # The texts of the new screens, in one file
│   │   ├── sidebar_view.*        # The side menu as native buttons (Tab, arrows, Enter)
│   │   └── *_view.*              # One per screen: draws and creates the controls from the `session/` model (`memory_view` is the "UMA memory & CPU cores" tab)
│   ├── windows/                  # Wrappers around the Windows API
│   │   ├── nt_defs.hpp           # Typedefs and undocumented native NTDLL calls
│   │   ├── system_counters.*     # Readings for the report and the GUI: processes in EcoQoS, games with Direct Flip, L3 cache (with CPUID fallback), DIMM slots via SMBIOS and window ids
│   │   ├── text_convert.cpp      # Implements `ports/text_convert.hpp`
│   │   ├── console_prompt.*      # `ClassifyStdin` and `ConsoleKeyInput`: the real console for a question with a time limit
│   │   ├── unique_handle.hpp     # Automatic closing of handles, registry keys and searches (RAII)
│   │   ├── device_probe.*        # Real Windows probe (DXCore COM, DXGI, SetupAPI RemovalPolicy, Storage SeekPenalty/BusType, Power, IsWow64Process2)
│   │   ├── memory.*              # Windows implementation of MemoryProbe and MemoryControl (ntdll, DXGI, working sets)
│   │   ├── process_control.*     # Windows implementation of ProcessControl, checking `GetProcessTimes` (creation_time)
│   │   ├── sandbox_guard.*       # Checked file I/O: normalizes the path, asks Rust for the decision and checks the open file again
│   │   ├── safe_file_system.*    # Real file port: everything goes through SandboxGuard
│   │   ├── config_location.*     # Path of config.json next to the .exe, and whether only administrators can write it
│   │   ├── change_journal.*      # Atomic journal in `<exe>\config\change_journal.vfj` and Windows undo (`WindowsSystemUndo`)
│   │   ├── steam_paths.*         # Steam installation folder and its libraries (reads libraryfolders.vdf with Rust)
│   │   ├── registry.*            # Windows implementation of `RegistryPort` (HKLM/HKCU, read/write/delete)
│   │   ├── power.*               # Windows implementation of `PowerPort` (PowrProf: plans and AC/DC)
│   │   └── privileges.*          # NT privilege management and UAC elevation
│   └── bridge/
│       ├── rust_bridge.h         # C/Rust type definitions and FFI signatures (zero-allocation)
│       ├── hardware_policy.*     # The hardware decisions Rust takes (device profile, CPU Set split), converted to and from the model types
│       └── ffi_callback_guard.hpp # C++ callbacks that Rust calls must never throw; the body runs inside RunCallbackGuarded
├── tests/
│   ├── test_main.cpp             # Entry point of the suite: runs all the tests in order and restores the system at the end
│   ├── test_support.*            # Counters, RecordResult and the reader of the .tsv files
│   ├── integration/              # Tests that really touch Windows, one file per module
│   ├── unit/                     # Tests with fake ports (they do not write game files or the registry; at most one empty temporary folder)
│   ├── package/                  # package_tests.ps1: end-to-end test of the zip (installs, applies a change, uninstalls and compares)
│   └── vectors/                  # Cases shared with the Rust tests
├── logs/
│   └── session_reports.log       # Persistent record of sessions and telemetry
├── build/
│   ├── vegaflow_daemon.exe       # Main executable
│   └── vegaflow_tests.exe        # Test verification executable
└── dist/                         # Output of package.sh: the zip, its .sha256 and the folder being compressed (outside git)
```

---
