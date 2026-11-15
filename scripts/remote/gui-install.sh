#!/bin/bash
# Installs only the job package on the laptop (manifest and scripts), without copying a daemon over the installed one
# (vfdev deploy copies the laptop repository's build\vegaflow_daemon.exe when the daemon is not running).
# Needs WinBridge's source checkout: WINBRIDGE_SRC=<winbridge> (for the runner.ps1 SHA-256, never taken from the laptop).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
set -a; . "$HERE/.env"; set +a
: "${WINBRIDGE_SRC:?set WINBRIDGE_SRC to the WinBridge source checkout}"
SHA=$(shasum -a 256 "$WINBRIDGE_SRC/bootstrap/jobs/runner.ps1" | cut -c1-64)
R() { ssh -i "$VFJOB_KEY" -o BatchMode=yes -o ConnectTimeout=10 "$VFJOB_HOST" "$@"; }
C() { scp -q -i "$VFJOB_KEY" -o BatchMode=yes "$@"; }
R 'if not exist C:\dev\vf-pkg\scripts mkdir C:\dev\vf-pkg\scripts'
C "$HERE/winbridge-package/manifest.json" "$HERE/busy.html" "$REPO/scripts/tweaks_state.ps1" "$REPO/scripts/ab_profile.ps1" "$HERE/deploy_package.ps1" "$VFJOB_HOST:C:/dev/vf-pkg/"
C "$HERE"/jobs/*.ps1 "$REPO/scripts/bench.ps1" "$REPO/scripts/bench_pack.ps1" "$REPO/scripts/ab_loop_task.ps1" "$VFJOB_HOST:C:/dev/vf-pkg/scripts/"
R "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\dev\\vf-pkg\\deploy_package.ps1 -Repo C:\\dev\\no-daemon-here -RunnerSha256 $SHA" 2>&1 | LC_ALL=C grep -a 'daemon:\|runner_sha256\|C:\\ProgramData'
