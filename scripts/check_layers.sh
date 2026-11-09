#!/bin/bash
# Checks that every #include "folder/..." in src/ respects the layer table in docs/plan/maestro/arquitectura.md (section 3b):
# a folder may only include the folders listed for it. Exits 1 and lists every broken include.
# Runs in CI (.github/workflows/ci.yml). Bash 3.2 compatible (macOS).
set -u
cd "$(dirname "$0")/.." || exit 2

# Folders each layer may include (its own folder is always allowed).
# "Migration" entries are the old folders (detect/, optimize/, safety/): they go away phase by phase
# (docs/plan/maestro/) and must be removed from these lists when they do.
allowed() {
    case "$1" in
    app) echo "session ui model windows bridge detect optimize safety" ;;
    ui) echo "session model" ;;
    session) echo "model ports bridge detect optimize safety windows" ;; # Migration: detect optimize safety windows
    ports) echo "model" ;;
    windows) echo "ports model bridge" ;;
    model) echo "" ;;
    bridge) echo "" ;;
    detect) echo "model windows bridge safety" ;;
    optimize) echo "detect model windows bridge safety" ;;
    safety) echo "model windows bridge" ;;
    *) echo "UNKNOWN" ;;
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
    ok="$(allowed "$from")"
    if [ "$ok" = "UNKNOWN" ]; then
        echo "unknown layer: src/$from (add it to scripts/check_layers.sh)"; fail=1; continue
    fi
    case " $ok " in *" $to "*) ;; *) echo "$file: $from/ must not include $to/ (${line#*:})"; fail=1 ;; esac
done < <(grep -rnE '^[[:space:]]*#[[:space:]]*include[[:space:]]*[<"][^">/]+/' src --include='*.cpp' --include='*.hpp' --include='*.h' --include='*.inl' --include='*.ipp')

# Reading nothing means grep or the folder failed, not that every layer is clean.
[ "$checked" -gt 0 ] || { echo "no project #include found under src/: nothing was checked"; exit 2; }
[ "$fail" = 0 ] && echo "[OK] layers respected ($checked includes checked)"
exit "$fail"
