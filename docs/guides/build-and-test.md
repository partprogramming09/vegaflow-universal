# Build and test

**In short:** This page explains how to turn VegaFlow's source code into the Windows program and how to run its test suite. On a Windows PC, `scripts/build.ps1` does both steps with one command. From a Mac or Linux, `scripts/build-cross.sh` builds the same files for Windows; this is called *cross-compiling*. For example, `powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests` builds the daemon (the background program) and the test suite, then runs the C++ tests.

**Reader:** people

## 5. Build, linking and test verification

VegaFlow Universal is written in **C++20** (a version of the C++ programming language) and linked statically on Windows. *Static linking* copies the libraries it needs into the `.exe`, so it needs no large external runtime. The Microsoft C runtime (MSVCRT / VCRedist), which normally has to be installed separately, is not required with MinGW.

### 5.1. Recommended toolchain

A *toolchain* is the set of tools that compile and link the code.

- **Compiler:** GCC 13 or newer (MinGW-w64, the Windows build of the GCC compiler), with full C++20 support. The Mac cross-compiler used on 2026-11-20 is `x86_64-w64-mingw32-g++ (GCC) 16.2.0`.
- **System libraries required** (the `-l` flags tell the linker which Windows libraries to use):
  `-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -lws2_32 -luserenv -lbcrypt`

### 5.2. Automated build (PowerShell)

The project includes the script [`scripts/build.ps1`](https://github.com/partprogramming09/vegaflow-universal/blob/main/scripts/build.ps1). It finds MinGW g++ (in `PATH`, in Scoop or in `C:\msys64`), checks that `cargo` (Rust's build tool) is installed, builds the Rust core, and then builds both executables. It finds every `.cpp` file under `src/` on its own. With `-RunTests`, it also runs `cargo test` on the Rust core before running the C++ suite.

```powershell
# Build vegaflow_daemon.exe and vegaflow_tests.exe:
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1

# Build and run the C++ suite right away (the last recorded run, 2026-11-19, printed
# `TEST RESULTS: 315 PASSED, 0 FAILED`; the number grows with each new test, so don't copy it from here):
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -RunTests
```

#### Building from a Mac or Linux (cross-compiling)

```bash
brew install mingw-w64                      # once
rustup target add x86_64-pc-windows-gnu     # once
scripts/build-cross.sh                      # builds build/vegaflow_daemon.exe and build/vegaflow_tests.exe
```

It uses the same flags and libraries as `build.ps1` and compiles in parallel. It finds the sources with `find` over `src/` and `tests/`, so a new `.cpp` file is included without editing any list. You run the `.exe` files on a Windows PC; you don't need to compile there.

#### Continuous integration (GitHub Actions)

The file `.github/workflows/ci.yml` runs three jobs on GitHub's machines: the layer check (`scripts/check_layers.sh`), the Rust core tests, and the cross-compilation of the daemon and the suite. The C++ tests don't run there. They change the Windows registry (the settings database) and the power plan (the energy profile) of a real Windows machine, and they need a GPU. So they are run by hand on the test laptop. A public repository must not register that laptop as its own runner, because any *fork* (a copy of the repository) could run code on it.

#### Testing only the Rust core (any machine, including a Mac)

```bash
cargo test --manifest-path rust/vegaflow-core/Cargo.toml
```

The Rust core makes no calls to `ntdll` (a Windows system library). The only mention of it in the Rust code is a comment (`rust/vegaflow-core/src/policy/memory_purge.rs:5`). The calls are in the C++ code, for example `src/windows/memory.cpp:88` and `:101`. So the rest of the logic (the Tiers, the path blacklist and the other rules in `rust/vegaflow-core/src/policy/`) runs without any Windows call. On a Mac, `cargo test` gave 258 passed, 0 failed (2026-11-20).

The cases for the Tier classification and for the path blacklist come from `tests/vectors/apu_tier.tsv` and `tests/vectors/sandbox_blacklist.tsv`. The Rust tests and C++ tests 32 and 33 both read them (`rust/vegaflow-core/src/policy/apu_tier.rs:4` and `path_policy.rs:6`). If the two implementations disagree, one of the two tests fails.

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
LIBS="-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -lws2_32 -luserenv -lbcrypt"
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

The suite prints one `[PASS]` or `[FAIL]` line per test and ends with a summary. The last line should read as follows (last recorded run, 2026-11-19):

```
TEST RESULTS: 315 PASSED, 0 FAILED
```

Where each test lives:
- `tests/integration/`: these touch the real Windows (the registry, the power plan, processes), one file per module. They are the tests that change the real registry.
- `tests/unit/`: these use fake ports (stand-ins for the file system, the registry and the clock), so they don't write game files or the registry.
- `tests/test_main.cpp`: runs the tests in order. At the end, a guard returns the system to its previous state. Don't interrupt it halfway.

The Rust rules are tested separately with `cargo test` (258 tests passed on 2026-11-20), and the layers of `src/` are checked with `scripts/check_layers.sh`.

---

## 7. The installation package

The package is built on a Mac or Linux, with the same tools that `scripts/build-cross.sh` needs (mingw-w64 and the Rust target), plus `zip` and `shasum` (a SHA-256 hash tool):

```bash
scripts/package.sh                     # builds dist/VegaFlow-<version>-win-x64.zip and its .sha256
```

Before it compiles anything, the script requires the version to match in three places: `rust/vegaflow-core/Cargo.toml`, `src/ui/app_identity.hpp`, and an entry `## [version]` in `CHANGELOG.md`. If not, it stops. Then it runs `build-cross.sh` and gathers into `dist/` the daemon, `install.ps1` and `uninstall.ps1` (from `scripts/package/`), `config/config.json`, `VERSION`, the license, `CHANGELOG.md` and `README.md`. It writes `SHA256SUMS`, which holds the SHA-256 hash of each of those 8 files, and then compresses everything. `dist/` is not uploaded to git.

To check the package, use `tests/package/package_tests.ps1`. It installs, applies a real change, uninstalls, and compares the system with its earlier state. It must run as administrator on a real Windows machine. How to use it, what it checks and its result are in [`install-and-uninstall.md`](./install-and-uninstall.md).

---
