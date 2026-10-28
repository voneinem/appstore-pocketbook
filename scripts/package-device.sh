#!/usr/bin/env bash
#
# Assemble the store's own app package for the InkPad One from a freshly built ARM program (research R-15, R-04).
#
#   scripts/package-device.sh [--allow-test-version] <built-arm-elf> [version] [out-dir] [probe-elf]
#   scripts/package-device.sh --check-size <zip> [limit-bytes]   fail when the zip is larger than the budget (25 MB)
#   scripts/package-device.sh --self-test                        prove that the size check works
#
# Produces <out-dir>/pbstore-inkpad-one-<version>.zip, laid out as the PB Store package contract asks: manifest.json and the
# applications/ tree at the top level, nothing else (a package may write only under applications/):
#
#   manifest.json                          made from packaging/manifest.template.json (id pbstore, data_dir null)
#   applications/pbstore.bin               the program (ARM ELF): the staging copy, never executed; the launcher runs a private copy
#   applications/pbstore.app               the launcher (shell script), the menu entry's path
#   applications/icons/pbstore.bmp, pbstore_f.bmp   launcher icons
#   applications/pbstore-probe.bin         the device probe (only in a dev package, 4th argument)
#
# The CA bundle and the license texts are compiled into the program (so a running program never reads a package file that an update
# could replace under it). The zip is written by `pbstore-catalog pack` (tool from this repository: PBSTORE_CATALOG or PATH), packed
# twice (the two results must be identical), checked with `pbstore-catalog check-package` and against the size budget.
#
# Release and dev packages (spec FR-007, plan flag): a RELEASE package has the version of app/VERSION, no probe, and goes to dist/.
# A DEV or TEST package has a version strictly below app/VERSION (so the real release installs over it as an update), may carry the
# probe, must be built with -DPBSTORE_DEV_BUILD=ON, and goes to dist-dev/, never to dist/. A version above app/VERSION is refused
# unless --allow-test-version is given. A probe is refused in any package whose version is not strictly below app/VERSION.
set -euo pipefail

MAX_ZIP_BYTES="${PBSTORE_MAX_ZIP_BYTES:-26214400}"   # 25 MB

check_zip_size() {  # check_zip_size <zip> [limit]
    local zip="$1" limit="${2:-$MAX_ZIP_BYTES}" size
    size="$(stat -c %s "$zip")"
    echo "package size: $size bytes (limit $limit)"
    if [ "$size" -gt "$limit" ]; then
        echo "the package is larger than the budget of $limit bytes: $zip" >&2
        return 1
    fi
    return 0
}

if [ "${1:-}" = "--check-size" ]; then
    check_zip_size "${2:?usage: package-device.sh --check-size <zip> [limit-bytes]}" "${3:-$MAX_ZIP_BYTES}"
    exit $?
fi

if [ "${1:-}" = "--self-test" ]; then
    tmp="$(mktemp -d)"
    case "$tmp" in /tmp/?*|/var/tmp/?*) ;; *) echo "unexpected temporary folder '$tmp'" >&2; exit 1 ;; esac
    trap 'rm -rf "$tmp"' EXIT
    head -c 1000 /dev/zero > "$tmp/small.zip"
    head -c 3000 /dev/zero > "$tmp/big.zip"
    check_zip_size "$tmp/small.zip" 2000 >/dev/null || { echo "self-test failed: a small package was refused" >&2; exit 1; }
    if check_zip_size "$tmp/big.zip" 2000 >/dev/null 2>&1; then echo "self-test failed: a package over the limit was accepted" >&2; exit 1; fi
    check_zip_size "$tmp/small.zip" 1000 >/dev/null || { echo "self-test failed: a package exactly at the limit was refused" >&2; exit 1; }
    echo "package-device.sh self-test passed"
    exit 0
fi

allow_test_version=0
if [ "${1:-}" = "--allow-test-version" ]; then allow_test_version=1; shift; fi

BIN="${1:?usage: package-device.sh [--allow-test-version] <built-arm-elf> [version] [out-dir] [probe-elf]}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
APPV="$(head -n 1 "$ROOT/app/VERSION" 2>/dev/null | tr -d '[:space:]')"
[[ "$APPV" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "app/VERSION is not MAJOR.MINOR.PATCH: '$APPV'" >&2; exit 1; }
VERSION="${2:-$APPV}"
[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "version is not MAJOR.MINOR.PATCH: $VERSION" >&2; exit 1; }
PROBE_BIN="${4:-}"

# which kind of package this is
semver_cmp() {  # prints -1, 0 or 1
    if [ "$1" = "$2" ]; then echo 0; elif [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n 1)" = "$1" ]; then echo -1; else echo 1; fi
}
rel="$(semver_cmp "$VERSION" "$APPV")"
dev=0
[ "$rel" != 0 ] && dev=1
[ -n "$PROBE_BIN" ] && dev=1
if [ -n "$PROBE_BIN" ] && [ "$rel" != -1 ]; then
    echo "a package with the probe must have a version strictly below app/VERSION ($APPV); a probe never ships in a release" >&2; exit 1
fi
if [ "$rel" = 1 ] && [ "$allow_test_version" != 1 ]; then
    echo "version $VERSION is above app/VERSION ($APPV); a test package with such a version needs --allow-test-version" >&2; exit 1
fi
if [ -n "${3:-}" ]; then OUT_ARG="$3"; elif [ "$dev" = 1 ]; then OUT_ARG="dist-dev"; else OUT_ARG="dist"; fi
out_base="$(basename "$OUT_ARG")"
if [ "$dev" = 1 ] && [ "$out_base" = "dist" ]; then
    echo "a dev or test package goes to dist-dev/, never to dist/" >&2; exit 1
fi
if [ "$dev" = 0 ] && [ "$out_base" = "dist-dev" ]; then
    echo "a release package goes to dist/, not to dist-dev/" >&2; exit 1
fi

# the store's tool: PBSTORE_CATALOG if set, else PATH
if [ -n "${PBSTORE_CATALOG:-}" ]; then
    [ -f "$PBSTORE_CATALOG" ] && [ -x "$PBSTORE_CATALOG" ] || { echo "PBSTORE_CATALOG is set but is not an executable file: $PBSTORE_CATALOG" >&2; exit 1; }
    PBSTORE="$PBSTORE_CATALOG"
else
    PBSTORE="$(command -v pbstore-catalog || true)"
    [ -n "$PBSTORE" ] || { echo "pbstore-catalog not found. Put it on PATH or set PBSTORE_CATALOG to the tool (build/tools/pbstore-catalog/pbstore-catalog)." >&2; exit 1; }
fi

for f in "$BIN" "$ROOT/platform/pocketbook/pbstore.app" "$ROOT/res/icons/pbstore.bmp" "$ROOT/res/icons/pbstore_f.bmp" "$ROOT/packaging/manifest.template.json"; do
    [ -f "$f" ] || { echo "missing input: $f" >&2; exit 1; }
done
[ -z "$PROBE_BIN" ] || [ -f "$PROBE_BIN" ] || { echo "missing input: $PROBE_BIN" >&2; exit 1; }

# the program must say what it is: version, release date, keys, no override or test name, the dev marker exactly when it is a dev build
check_args=(--version "$VERSION")
[ "$dev" = 1 ] && check_args+=(--dev)
"$ROOT/scripts/check-store-binary.sh" "${check_args[@]}" "$BIN" >&2

OUT_DIR="$(mkdir -p "$OUT_ARG" && cd "$OUT_ARG" && pwd)"
ZIP="$OUT_DIR/pbstore-inkpad-one-$VERSION.zip"

stage="$(mktemp -d)"
work="$(mktemp -d)"
case "$stage$work" in /tmp/?*/tmp/?*|/var/tmp/?*/var/tmp/?*|/tmp/?*/var/tmp/?*|/var/tmp/?*/tmp/?*) ;; *) echo "unexpected temporary folders" >&2; exit 1 ;; esac
trap 'rm -rf "$stage" "$work"' EXIT
apps="$stage/applications"
mkdir -p "$apps/icons"
install -m 777 "$BIN"                                   "$apps/pbstore.bin"
install -m 777 "$ROOT/platform/pocketbook/pbstore.app"  "$apps/pbstore.app"
[ -z "$PROBE_BIN" ] || install -m 777 "$PROBE_BIN"      "$apps/pbstore-probe.bin"
install -m 666 "$ROOT/res/icons/pbstore.bmp"            "$apps/icons/pbstore.bmp"
install -m 666 "$ROOT/res/icons/pbstore_f.bmp"          "$apps/icons/pbstore_f.bmp"
sed "s/@VERSION@/$VERSION/g" "$ROOT/packaging/manifest.template.json" > "$stage/manifest.json"
left="$(grep -Eo '@[A-Z_]+@' "$stage/manifest.json" | head -n 1 || true)"
[ -z "$left" ] || { echo "manifest template has an unfilled placeholder: $left" >&2; exit 1; }

# a ZIP of the same version from an earlier build is replaced, never kept next to a failed build
rm -f "$ZIP"
# pack twice: the tool must give the same zip for the same files
for n in a b; do
    "$PBSTORE" pack "$stage" --out "$work/$n.zip" >&2 || { echo "pbstore-catalog pack failed" >&2; exit 1; }
done
cmp -s "$work/a.zip" "$work/b.zip" || { echo "package is not reproducible: two builds from the same files differ" >&2; exit 1; }
"$PBSTORE" check-package "$work/a.zip" >&2 || { echo "package check failed" >&2; exit 1; }
check_zip_size "$work/a.zip" >&2 || exit 1
mv "$work/a.zip" "$ZIP"
echo "$ZIP"
