#!/bin/bash
# Builds dist/VegaFlow-<version>-win-x64.zip from the cross-compiled daemon and scripts/package/.
# The version comes from rust/vegaflow-core/Cargo.toml and must match src/ui/app_identity.hpp and the top CHANGELOG entry.
# Needs what scripts/build-cross.sh needs (mingw-w64, the Rust target) plus zip and shasum.
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="$(sed -n 's/^version = "\(.*\)"/\1/p' rust/vegaflow-core/Cargo.toml | head -n1)"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "error: no MAJOR.MINOR.PATCH in Cargo.toml: '$VERSION'" >&2; exit 1; }
grep -q "kVersion\[\] = \"$VERSION\"" src/ui/app_identity.hpp || { echo "error: src/ui/app_identity.hpp kVersion is not $VERSION" >&2; exit 1; }
grep -q "^## \[$VERSION\]" CHANGELOG.md || { echo "error: CHANGELOG.md has no entry for $VERSION" >&2; exit 1; }

bash scripts/build-cross.sh

NAME="VegaFlow-$VERSION-win-x64"
STAGE="dist/$NAME"
rm -rf "$STAGE" "dist/$NAME.zip" "dist/$NAME.zip.sha256"
mkdir -p "$STAGE/config"
cp build/vegaflow_daemon.exe "$STAGE/"
cp scripts/package/install.ps1 scripts/package/uninstall.ps1 "$STAGE/"
cp config/config.json "$STAGE/config/"
cp LICENSE CHANGELOG.md README.md "$STAGE/"
printf '%s\n' "$VERSION" > "$STAGE/VERSION"

# install.ps1 checks every file listed here before it copies anything.
(cd "$STAGE" && shasum -a 256 vegaflow_daemon.exe install.ps1 uninstall.ps1 config/config.json VERSION LICENSE CHANGELOG.md README.md > SHA256SUMS)

(cd dist && zip -q -r -X "$NAME.zip" "$NAME")
(cd dist && shasum -a 256 "$NAME.zip" > "$NAME.zip.sha256")
echo "[OK] dist/$NAME.zip"
cat "dist/$NAME.zip.sha256"
