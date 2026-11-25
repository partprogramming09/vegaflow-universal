#!/bin/bash
# Checks that every #include "folder/..." in src/ respects the layer table in docs/guides/modules/index.md ("Layer table"):
# a folder may only include the folders listed for it. Exits 1 and lists every broken include.
# Runs in CI (.github/workflows/ci.yml). Bash 3.2 compatible (macOS).
set -u
cd "$(dirname "$0")/../.." || exit 2

# Folders each layer may include (its own folder is always allowed).
# detect/ and optimize/ are the old folders: they go away phase by phase (and drop out of these lists when they do).
allowed() {
    case "$1" in
    app) echo "session ui model ports windows bridge detect optimize" ;;
    ui) echo "session model" ;;
    session) echo "model ports bridge" ;; # strict; the debt() list below is the only way around it
    ports) echo "model" ;;
    windows) echo "ports model bridge" ;;
    model) echo "" ;;
    bridge) echo "" ;;
    detect) echo "model windows bridge" ;;
    optimize) echo "model ports windows bridge" ;;
    *) echo "UNKNOWN" ;;
    esac
}

# Debt (F4.3): the session/ files that still include detect/, optimize/ or windows/ because the class they use has no port yet.
# Every other session/ file is strict. This list only shrinks: a file leaves it when its last include is gone; never add one.
debt() {
    case "$1" in
    src/session/reports.cpp) echo "detect optimize windows" ;;        # concrete optimizers, ProcessDetector, privileges, registry/power adapters
    src/session/run_cycle.cpp) echo "detect optimize windows" ;;      # same, plus WindowsDeviceProbe and system_counters
    src/session/run_cycle.hpp) echo "detect optimize" ;;              # RunCycle takes CpuOrchestrator&, MemoryManager& and HardwareProfiler types
    src/session/system_info.cpp) echo "detect optimize windows" ;;    # GraphicsOptimizer, HardwareProfiler, system_counters, adapters
    src/session/game_session.cpp) echo "detect optimize windows" ;;   # CpuOrchestrator, HardwareProfiler, WindowThreadId
    src/session/settings_actions.hpp) echo "optimize" ;;              # SettingsActionsDeps holds GraphicsOptimizer&
    esac
}

fail=0
checked=0
while IFS= read -r line; do
    file="${line%%:*}"
    from="$(echo "$file" | cut -d/ -f2)"
    to="$(echo "$line" | sed -E 's/.*#[[:space:]]*include[[:space:]]*[<"]([^/">]+)\/.*/\1/')"
    [ -d "src/$to" ] || continue # <sys/...>, "../x" and other non-layer paths
    checked=$((checked + 1))
    [ "$from" = "$to" ] && continue
    ok="$(allowed "$from") $(debt "$file")"
    [ "$(allowed "$from")" = "UNKNOWN" ] && ok="UNKNOWN"
    if [ "$ok" = "UNKNOWN" ]; then
        echo "unknown layer: src/$from (add it to scripts/ci/check_layers.sh)"; fail=1; continue
    fi
    case " $ok " in *" $to "*) ;; *) echo "$file: $from/ must not include $to/ (${line#*:})"; fail=1 ;; esac
done < <(grep -rnE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">/]+/' src --include='*.cpp' --include='*.hpp' --include='*.h' --include='*.inl' --include='*.ipp')

# A debt entry whose includes are gone must be deleted, so the list cannot hide new debt behind an old name.
for file in src/session/reports.cpp src/session/run_cycle.cpp src/session/run_cycle.hpp src/session/system_info.cpp src/session/game_session.cpp src/session/settings_actions.hpp; do
    for to in $(debt "$file"); do
        grep -qE "^[[:space:]]*#[[:space:]]*include[[:space:]]*[\"<]$to/" "$file" || { echo "$file: debt() lists $to/ but the file no longer includes it: remove it"; fail=1; }
    done
done

# Reading nothing means grep or the folder failed, not that every layer is clean.
[ "$checked" -gt 0 ] || { echo "no project #include found under src/: nothing was checked"; exit 2; }
[ "$fail" = 0 ] && echo "[OK] layers respected ($checked includes checked)"
exit "$fail"
