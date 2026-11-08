#!/bin/bash
# Cross-compile the Windows daemon and the test suite on macOS/Linux with mingw-w64.
# Same flags and libraries as scripts/build.ps1; the sources are found by globbing src/ so a new .cpp is
# picked up without editing a list. Output: build/vegaflow_daemon.exe and build/vegaflow_tests.exe.
#
# Needs: x86_64-w64-mingw32-g++ (e.g. `brew install mingw-w64`) and the Rust target
#        (`rustup target add x86_64-pc-windows-gnu`). Override the compiler with VF_CXX.
set -euo pipefail
cd "$(dirname "$0")/.."

CXX="${VF_CXX:-x86_64-w64-mingw32-g++}"
TARGET="x86_64-pc-windows-gnu"
JOBS="${VF_JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)}"

command -v "$CXX" >/dev/null || { echo "error: $CXX not found (install mingw-w64)" >&2; exit 1; }
rustup target list --installed 2>/dev/null | grep -q "^$TARGET$" || { echo "error: run 'rustup target add $TARGET'" >&2; exit 1; }

echo "[*] Building the Rust core for $TARGET..."
cargo build --release --manifest-path rust/vegaflow-core/Cargo.toml --target "$TARGET" --quiet
RUST_LIB="rust/vegaflow-core/target/$TARGET/release/libvegaflow_core.a"

COMPILE_FLAGS=(-std=c++20 -O2 -Wall -Wextra -Isrc)
LINK_FLAGS=(-static -static-libgcc -static-libstdc++)
LIBS=(-lntdll -ldxgi -ladvapi32 -lpsapi -luser32 -lole32 -lgdi32 -lshell32 -ldwmapi -ltdh -lws2_32 -luserenv -lbcrypt)

OBJ_DIR="build/obj"
rm -rf "$OBJ_DIR"
mkdir -p "$OBJ_DIR"

echo "[*] Compiling with $JOBS parallel jobs ($CXX)..."
SOURCES=$(find src tests -name '*.cpp' | sort)
export CXX OBJ_DIR
export COMPILE_FLAGS_STR="${COMPILE_FLAGS[*]}"
printf '%s\n' $SOURCES | xargs -P "$JOBS" -I{} bash -c '
    src="$1"; obj="$OBJ_DIR/$(echo "$src" | tr / _).o"
    $CXX $COMPILE_FLAGS_STR -c "$src" -o "$obj" || exit 255
' _ {}

objects_of() { for f in "$@"; do echo "$OBJ_DIR/$(echo "$f" | tr / _).o"; done; }
COMMON=$(find src -name '*.cpp' -not -path 'src/app/*' | sort)

echo "[*] Linking build/vegaflow_daemon.exe and build/vegaflow_tests.exe..."
"$CXX" "${LINK_FLAGS[@]}" $(objects_of $COMMON src/app/*.cpp) "$RUST_LIB" -o build/vegaflow_daemon.exe "${LIBS[@]}"
"$CXX" "${LINK_FLAGS[@]}" $(objects_of $COMMON tests/test_main.cpp) "$RUST_LIB" -o build/vegaflow_tests.exe "${LIBS[@]}"
echo "[OK] build/vegaflow_daemon.exe and build/vegaflow_tests.exe"
