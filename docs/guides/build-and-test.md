# Build and test

**In short:** This page explains how to turn VegaFlow's source code into the Windows program and how to run its test suite. On a Windows PC, `scripts/build/build.ps1` does both steps with one command. From a Mac or Linux, `scripts/build/build-cross.sh` builds the same files for Windows; this is called *cross-compiling*. For example, `powershell -ExecutionPolicy Bypass -File .\scripts\build\build.ps1 -RunTests` builds the daemon (the background program) and the test suite, then runs the C++ tests.

**Reader:** people

## 5. Build, linking and test verification

VegaFlow Universal is written in **C++20** (a version of the C++ programming language) and linked statically on Windows. *Static linking* copies the libraries it needs into the `.exe`, so it needs no large external runtime. The Microsoft C runtime (MSVCRT / VCRedist), which normally has to be installed separately, is not required with MinGW.

### 5.1. Recommended toolchain

A *toolchain* is the set of tools that compile and link the code.

- **Compiler:** GCC 13 or newer (MinGW-w64, the Windows build of the GCC compiler), with full C++20 support. The Mac cross-compiler used on 2026-11-20 is `x86_64-w64-mingw32-g++ (GCC) 16.2.0`.
- **System libraries required** (the `-l` flags tell the linker which Windows libraries to use):
  `-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -lws2_32 -luserenv -lbcrypt`
  `-lntdll` is needed even though the C++ code loads its `Nt*` functions at run time: the Rust standard library inside `libvegaflow_core.a` imports `NtCreateFile`, `NtWriteFile` and `RtlNtStatusToDosError` directly (checked with `nm -u` and `objdump -p` on 2026-11-20).

### 5.2. Automated build (PowerShell)

The project includes the script [`scripts/build/build.ps1`](https://github.com/partprogramming09/vegaflow-universal/blob/main/scripts/build/build.ps1). It finds MinGW g++ (in `PATH`, in Scoop or in `C:\msys64`), checks that `cargo` (Rust's build tool) is installed, builds the Rust core, and then builds both executables. It finds every `.cpp` file under `src/` on its own. With `-RunTests`, it also runs `cargo test` on the Rust core before running the C++ suite.

```powershell
# Build vegaflow_daemon.exe and vegaflow_tests.exe:
powershell -ExecutionPolicy Bypass -File .\scripts\build\build.ps1

# Build and run the C++ suite right away (the last recorded run, 2026-11-19, printed
# `TEST RESULTS: 419 PASSED, 0 FAILED`; the number grows with each new test, so don't copy it from here):
powershell -ExecutionPolicy Bypass -File .\scripts\build\build.ps1 -RunTests
```

#### Building from a Mac or Linux (cross-compiling)

```bash
brew install mingw-w64                      # once
scripts/build/build-cross.sh                      # builds build/vegaflow_daemon.exe and build/vegaflow_tests.exe
```

The Rust version is pinned in `rust-toolchain.toml` (exact version and the `x86_64-pc-windows-gnu` target), so `rustup` installs both on the first `cargo` run and every machine and the CI use the same compiler. To upgrade, change that one line, run the CI and the laptop suite, and commit the bump alone.

It uses the same flags and libraries as `build.ps1` and compiles in parallel. It finds the sources with `find` over `src/` and `tests/`, so a new `.cpp` file is included without editing any list. You run the `.exe` files on a Windows PC; you don't need to compile there.

#### Running the Windows-free unit tests on a Mac or Linux

`scripts/ci/host-unit-tests.sh` compiles with the host's `clang++` (no Wine, no mingw) and runs the unit tests that never touch Windows, so a change can be seen failing and passing before the laptop run. It builds the Rust core for the host, links the allowed test files in `tests/host/allowlist.txt` against a header-only fake `windows.h`, prints `HOST UNIT TESTS: N PASSED, M FAILED` and the excluded files with a one-word reason, and exits 0 only if all pass. It also runs Test 34, which compares the size, alignment and every field offset of the `#[repr(C)]` structs in Rust with `src/bridge/rust_bridge.h`: the layout does not depend on Windows, so a field changed on one side only fails here. What stays only on the laptop is every test that calls Windows (registry, power plan, processes, GPU). It does not replace the full suite on the laptop.

#### Continuous integration (GitHub Actions)

The file `.github/workflows/ci.yml` runs five jobs on GitHub's machines: the layer check (`scripts/ci/check_layers.sh`), the Rust core tests, the Windows-free C++ unit tests (`scripts/ci/host-unit-tests.sh`, which includes the FFI layout check), the cross-compilation of the daemon and the suite, and the docs build. The C++ integration tests don't run there. They change the Windows registry (the settings database) and the power plan (the energy profile) of a real Windows machine, and they need a GPU. So they are run by hand on the test laptop. A public repository must not register that laptop as its own runner, because any *fork* (a copy of the repository) could run code on it.

#### Testing only the Rust core (any machine, including a Mac)

```bash
cargo test --manifest-path rust/vegaflow-core/Cargo.toml
```

The Rust core makes no calls to `ntdll` (a Windows system library). The only mention of it in the Rust code is a comment (in the module header of `rust/vegaflow-core/src/policy/memory_purge.rs`). The calls are in the C++ code, for example `ResolveNtQuerySystemInformation` and `ResolveNtSetSystemInformation` in `src/windows/memory.cpp`. So the rest of the logic (the Tiers, the path blacklist and the other rules in `rust/vegaflow-core/src/policy/`) runs without any Windows call. On a Mac, `cargo test` gave 258 passed, 0 failed (2026-11-20).

The cases for the Tier classification and for the path blacklist come from `tests/vectors/apu_tier.tsv` and `tests/vectors/sandbox_blacklist.tsv`. The Rust tests and C++ tests 32 and 33 both read them (in the module headers of `rust/vegaflow-core/src/policy/apu_tier.rs` and `path_policy.rs`). If the two implementations disagree, one of the two tests fails.

### 5.3. Manual build with MinGW G++

#### Build the Rust static library (required first step):

```bash
cargo build --manifest-path rust/vegaflow-core/Cargo.toml --target x86_64-pc-windows-gnu --release
```

#### Sources shared by the daemon and the tests

Everything in `src/` except `src/app/` (the daemon's entry points). With Git Bash or MSYS2:

```bash
COMMON="src/windows/*.cpp src/detect/*.cpp src/session/*.cpp src/optimize/*.cpp src/optimize/graphics/*.cpp src/optimize/steam/*.cpp src/ui/*.cpp"
FLAGS="-std=c++20 -O2 -Wall -Wextra -static -static-libgcc -static-libstdc++ -Isrc"
RUST=rust/vegaflow-core/target/x86_64-pc-windows-gnu/release/libvegaflow_core.a
LIBS="-lntdll -ldxgi -lpdh -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -lws2_32 -luserenv -lbcrypt -lcfgmgr32 -lsynchronization"
```

#### Build the daemon (`vegaflow_daemon.exe`):

```bash
g++ $FLAGS $COMMON src/app/*.cpp -o build/vegaflow_daemon.exe $RUST $LIBS
```

#### Build the test suite (`vegaflow_tests.exe`):

```bash
g++ $FLAGS $COMMON tests/*.cpp tests/integration/*.cpp tests/unit/*.cpp -o build/vegaflow_tests.exe $RUST $LIBS
```

If you add a new folder inside `src/`, add it to `COMMON`. `build.ps1` and `build-cross.sh` find it on their own.

---

## 6. The integrated test suite (required for every commit)

Every change to the repository must be checked by running `build/vegaflow_tests.exe`. All the tests must pass, with no exceptions.

The suite prints one `[PASS]` or `[FAIL]` line per test and ends with a summary. The last line should read as follows (last recorded run, 2026-11-22):

```
TEST RESULTS: 419 PASSED, 0 FAILED
```

Where each test lives:
- `tests/integration/`: these touch the real Windows (the registry, the power plan, processes), one file per module. They are the tests that change the real registry.
- `tests/unit/`: these use fake ports (stand-ins for the file system, the registry and the clock), so they don't write game files or the registry.
- `tests/test_main.cpp`: runs the tests in order. At the end, a guard returns the system to its previous state. Don't interrupt it halfway.

The Rust rules are tested separately with `cargo test` (295 tests passed on 2026-11-22), and the layers of `src/` are checked with `scripts/ci/check_layers.sh`.

---

## 7. The installation package

The package is built on a Mac or Linux, with the same tools that `scripts/build/build-cross.sh` needs (mingw-w64 and the Rust target), plus `zip` and `shasum` (a SHA-256 hash tool):

```bash
scripts/build/package.sh                     # builds dist/VegaFlow-<version>-win-x64.zip and its .sha256
```

Before it compiles anything, the script requires the version to match in three places: `rust/vegaflow-core/Cargo.toml`, `src/ui/app_identity.hpp`, and an entry `## [version]` in `CHANGELOG.md`. If not, it stops. Then it runs `build-cross.sh` and gathers into `dist/` the daemon, `install.ps1` and `uninstall.ps1` (from `scripts/package/`), `config/config.json`, `VERSION`, the license, `CHANGELOG.md` and `README.md`. It writes `SHA256SUMS`, which holds the SHA-256 hash of each of those 8 files, and then compresses everything. `dist/` is not uploaded to git.

To check the package, use `tests/package/package_tests.ps1`. It installs, applies a real change, uninstalls, and compares the system with its earlier state. It must run as administrator on a real Windows machine. How to use it, what it checks and its result are in [`install-and-uninstall.md`](./install-and-uninstall.md).

From the Mac, `scripts/remote/vfdev package-test [ZIP]` does the whole run on the test laptop with the script of your current branch (so the laptop does not need to be on that branch). It builds the zip with `package.sh` unless you give one (or set `VFJOB_PKG_ZIP`), copies the zip and `package_tests.ps1` to `C:\ProgramData\VegaFlow\pkgtest` (administrators only, because the job runs elevated), and runs the `package-test` job. The job checks the SHA-256 of both files first, unpacks the zip into a fresh folder, runs the test and keeps the whole output in `pkgtest\out\package-test-<date>-<time>.txt`. `vfdev` copies that file to `build/package-test/` (or `VFJOB_PKG_OUT`) and exits with 0 only if the output says `0 FAILED`. If the zip or the script is missing, does not match its hash or cannot be opened, the job exits with 2 and runs nothing. The `package-test` job must be installed once with `vfdev deploy`. The test does not need the GPU or the desktop session, but it changes the real registry for a few seconds (see above), so don't start it while a game is open.

The two checks that start a background daemon (`--elevate --observe` stays running, and a second start says "already running") used to wait a fixed 3 seconds before judging. They now wait until the first daemon owns its single-instance mutex, up to 30 seconds, and wait for each daemon to exit before the next step. The package test does not run on GitHub-hosted runners: its steps 5, 7 and 8 apply `--mpo-disable` to `HKLM` and compare the power plan, and the script has no mode that skips them.

---
