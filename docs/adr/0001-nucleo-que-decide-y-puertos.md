# ADR 0001: Rust decides, C++ acts through ports

**In short:** VegaFlow's C++ code mixed "what to do" with "how to talk to Windows", so no rule could be tested without touching the real system. This decision moves the rules into a Rust core and puts each Windows action behind a small interface, called a port, that a test can replace with a fake. For example, `EvaluateAndOptimize` used to decide and purge memory in the same place. After this decision the choice goes to the core, and the purge sits behind a memory port.

**Reader:** people. Date: 2026-11-08 (proposal). Status: **accepted** on 2026-11-14.

**Evidence of acceptance (2026-11-14):** phases 1 to 8 and R3 are done; `scripts/ci/check_layers.sh` passes (272 includes, layers respected), and `src/session/` no longer calls Win32 directly. Result in [modules](../guides/modules/).

## Context

VegaFlow is a Windows daemon (a background program) that runs as administrator. It is written in C++20 (about 15,000 lines) with a Rust core (7.5%). It uses no frameworks and is maintained by one person. The diagnosis of 2026-11-08 found five problems:

1. **Decisions are mixed with Windows calls** inside the same function. For example, `EvaluateAndOptimize` decides and purges in the same place, and the allowlist decides and also reads system variables. So a rule cannot be tested without touching the real system.
2. **Classes made only of static functions and with no interfaces:** `GraphicsOptimizer` had 36 static members, `SandboxGuard` 21, and not a single `virtual` function (a method a test can override). There was no way to swap the real registry for a test one. That is why the tests changed the real registry and power plan, and a final guard restored them.
3. **Circular dependency between `detect/` and `optimize/`.** `system_info.cpp` includes the optimizers, and the optimizers include `hardware_profiler.hpp` to use its types (`ApuOptimizationPolicy`).
4. **All tests in one file:** `tests/test_main.cpp`, 2,157 lines, 31 active tests and a single `main`.
5. **Orchestration is spread out.** `run_cycle.cpp` (the "game session" use case) lives in `app/` next to the CLI and the GUI. Also, `frame_telemetry` is in `safety/` even though it measures FPS.

## Decision

A **light hexagonal architecture** (also called "the core decides, the shell acts"; in the literature, ports and adapters):

```
+-------------------------------------------------------+
| Entries: CLI, GUI, daemon loop (src/app)              |
+-------------------------------------------------------+
                           |
                           v
+-------------------------------------------------------+
| Use cases (src/session): game session,                |
| game engine profile, restore, reports                 |
| They only talk to ports and to the core               |
+-------------------------------------------------------+
                |                          |
                v                          v
+---------------------------+  +---------------------------+
| Rust core (decides)       |  | Ports (src/ports):        |
| rust/vegaflow-core        |  | small C++ interfaces:     |
| pure rules, no I/O        |  | files, clock, processes,  |
| (the UMA purge moves      |  | registry, power, hardware,|
| into the core in phase 4) |  | memory, configuration,    |
|                           |  | and journal               |
+---------------------------+  +---------------------------+
                                             |
                                             v
                               +---------------------------+
                               | Windows adapters          |
                               | (src/windows): Win32,     |
                               | ntdll, ETW, DXGI          |
                               +---------------------------+
```

The rules of the decision:

- **Dependencies point inward.** Adapters know the ports, and use cases know the ports and the core. The core knows no one.
- **A port is created only where a test needs a stand-in:** files (with the path check already applied), clock, process control, registry, power, device probe (GPU, disk, power, Windows version), memory (measure and purge), configuration and the change journal. There is no interface per class.
- **Static classes become objects** that receive their ports in the constructor. Everything is assembled in one place (`main`, the so-called *composition root*).
- **Shared types** (hardware profile, policy, detected game) move to `src/model/`. This breaks the cycle between `detect/` and `optimize/`.
- **Tests on two levels:**
  - unit tests of the use cases with fake ports, which do not touch the system;
  - integration tests per adapter, run on the laptop.

  `test_main.cpp` is split by module. This was done in phase R1 (2026-11-09): `tests/integration/`, one file per module, with its own `main` (not doctest, a C++ test framework) to keep the order and the output. doctest will be evaluated with the unit tests of R2.

## Alternatives

| Option | In favor | Against |
|---|---|---|
| **Do nothing** | No risk and no effort | Tests keep touching the real registry and power plan, and rules stay impossible to test without touching the system |
| **Only move folders** | Cheap and mechanical | Fixes nothing at the root: decisions stay mixed with Windows |
| **Light hexagonal** (chosen) | Rules in Rust tested with examples, use cases testable without touching the system, no cycles | Needs about 10 new small interfaces, and turning static classes into objects |
| **Full hexagonal** (one interface per service and DI everywhere) | Maximum flexibility | Much repeated code for one person; goes against KISS (keep it simple) and YAGNI (you aren't going to need it) |
| **Everything in Rust** | One language | Rewrites 15,000 lines, the GUI included, and forces repeating all the measurements |

## Consequences

- Improvements are made **in pieces** (the *strangler* pattern: the new code grows around the old and replaces it bit by bit), within the phases of the master plan (working material, outside the repository). In each phase the decision moves into the core and the action goes behind its port.
- Moving files and changing logic always go in **separate commits**.
- The rule "dependencies point inward" is checked by a script in CI (`scripts/ci/check_layers.sh`). The header of the C++ ↔ Rust bridge is generated from Rust with cbindgen (a tool that writes C headers from Rust code), so the two sides cannot drift apart.
- Idle use must stay below 15 MB. Virtual calls cost nanoseconds, so they do not affect it, but it is measured at the end.
- **This decision is reviewed** if more than 12 ports are needed in total, if a single phase needs more than 2 new ones, or if idle use goes above 15 MB.
- **Reviewed 2026-11-09:** it reached 11 ports when configuration and the change journal were added, and dropped to 10 when frame capture was removed (phase 6). The decision stands, because each port replaces a test that touched the real system. New limit: 12.
