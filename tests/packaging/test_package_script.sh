#!/usr/bin/env bash
# scripts/package-device.sh and scripts/check-store-binary.sh with a fake ARM program (research R-15, tasks T053/T055):
# the package layout and manifest, the reproducible packing, the checks of the program (version, release date, no test-only
# or override names, the trusted keys, no dev marker in a release), the size budget, and the rules for dev and test packages.
#
#   tests/packaging/test_package_script.sh [repo-root]
set -u

ROOT="${1:-$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)}"
SCRIPT="$ROOT/scripts/package-device.sh"
CHECK="$ROOT/scripts/check-store-binary.sh"
tmp="$(mktemp -d)" || exit 1
case "$tmp" in /tmp/?*|/var/tmp/?*) ;; *) echo "unexpected temporary folder '$tmp'" >&2; exit 1 ;; esac
[ -d "$tmp" ] || exit 1
trap 'rm -rf "$tmp"' EXIT
fail=0
check() { if ! "$@"; then echo "FAIL (line ${BASH_LINENO[0]}): $*" >&2; fail=1; fi; }
check_not() { if "$@"; then echo "FAIL (line ${BASH_LINENO[0]}, should not hold): $*" >&2; fail=1; fi; }

[ -f "$SCRIPT" ] || { echo "FAIL: $SCRIPT does not exist" >&2; exit 1; }
[ -f "$CHECK" ] || { echo "FAIL: $CHECK does not exist" >&2; exit 1; }

# the catalog tool of this build
TOOL="${PBSTORE_CATALOG:-}"
if [ -z "$TOOL" ] && [ -n "${PBSTORE_BUILD_DIR:-}" ]; then TOOL="$PBSTORE_BUILD_DIR/tools/pbstore-catalog/pbstore-catalog"; fi
if [ -z "$TOOL" ] || [ ! -x "$TOOL" ]; then echo "SKIP: pbstore-catalog is not built (set PBSTORE_CATALOG or PBSTORE_BUILD_DIR)"; exit 77; fi
export PBSTORE_CATALOG="$TOOL"
command -v unzip >/dev/null 2>&1 || { echo "SKIP: unzip is not installed"; exit 77; }

APPV="$(head -n 1 "$ROOT/app/VERSION" | tr -d '[:space:]')"
DATE="$(head -n 1 "$ROOT/app/RELEASE_DATE" | tr -d '[:space:]')"
lower="0.0.1"                                   # a version below the release, for dev packages
above="99.0.0"

# the raw bytes of every trusted key, as the program has them compiled in
keys_bytes() { for k in "$ROOT"/keys/trusted/*.pub; do tr -d '[:space:]' < "$k" | base64 -d; done; }

# a fake program; $1 = version; the rest are extra strings put into it
fake() {
    v="$1"; shift
    { printf 'FAKE-ARM-ELF\0'; printf 'PBSTORE_APP_VERSION=%s\0' "$v"; printf 'PBSTORE_RELEASE_DATE=%s\0' "$DATE"; keys_bytes
      for s in "$@"; do printf '%s\0' "$s"; done; } > "$tmp/pbstore"
    chmod +x "$tmp/pbstore"
}

OUT="$tmp/dist"

# 1. a good package: layout, manifest, name, the checks that the script itself runs
fake "$APPV"
zip_path="$("$SCRIPT" "$tmp/pbstore" "$APPV" "$OUT" 2>"$tmp/err")"; code=$?
check [ "$code" = 0 ]
check [ "$zip_path" = "$OUT/pbstore-inkpad-one-$APPV.zip" ]
check [ -f "$zip_path" ]
if [ -f "$zip_path" ]; then
    list="$(unzip -Z1 "$zip_path")"
    check grep -qx 'manifest.json' <<<"$list"
    check grep -qx 'applications/pbstore.app' <<<"$list"
    check grep -qx 'applications/pbstore.bin' <<<"$list"
    check grep -qx 'applications/icons/pbstore.bmp' <<<"$list"
    check grep -qx 'applications/icons/pbstore_f.bmp' <<<"$list"
    check_not grep -q '^system/' <<<"$list"
    check_not grep -q 'probe' <<<"$list"
    manifest="$(unzip -p "$zip_path" manifest.json)"
    check grep -q '"id": *"pbstore"' <<<"$manifest"
    check grep -q '"name": *"PB Store"' <<<"$manifest"
    check grep -q "\"version\": *\"$APPV\"" <<<"$manifest"
    check grep -q '"min_firmware": *"6.11.1437"' <<<"$manifest"
    check grep -q '"target_device": *"InkPad One"' <<<"$manifest"
    check grep -q '"data_dir": *null' <<<"$manifest"
    check grep -q '"id": *"U_PbStore"' <<<"$manifest"
    check grep -q '"path": *"applications/pbstore.app"' <<<"$manifest"
    check grep -q '"icon": *"applications/icons/pbstore.bmp"' <<<"$manifest"
    check grep -q '"focused_icon": *"applications/icons/pbstore_f.bmp"' <<<"$manifest"
    check grep -q '"group": *"@General"' <<<"$manifest"
    check "$TOOL" check-package "$zip_path" >/dev/null 2>&1
    # the program in the package is the program that was built, and the launcher is the repository's
    unzip -p "$zip_path" applications/pbstore.bin | cmp -s - "$tmp/pbstore"; check [ "$?" = 0 ]
    unzip -p "$zip_path" applications/pbstore.app | cmp -s - "$ROOT/platform/pocketbook/pbstore.app"; check [ "$?" = 0 ]
fi

# 2. the same inputs give the same bytes
cp "$zip_path" "$tmp/first.zip" 2>/dev/null
"$SCRIPT" "$tmp/pbstore" "$APPV" "$OUT" >/dev/null 2>&1
check cmp -s "$tmp/first.zip" "$zip_path"

# 3. a program without its version string is refused, and nothing is written
fake "9.9.9"
rm -rf "$tmp/out3"
"$SCRIPT" "$tmp/pbstore" "$APPV" "$tmp/out3" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
check grep -q "says version '9.9.9'" "$tmp/err"
check_not test -e "$tmp/out3/pbstore-inkpad-one-$APPV.zip"

# 4. a program with a test-only name or an override name is refused
for bad in PBSTORE_TEST_CATALOG_URL PBSTORE_TEST_KEY PBSTORE_CATALOG_URL PBSTORE_CA_FILE PBSTORE_STATE_DIR; do
    fake "$APPV" "$bad"
    rm -rf "$tmp/out4"
    "$SCRIPT" "$tmp/pbstore" "$APPV" "$tmp/out4" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
    check grep -q "$bad" "$tmp/err"
    "$CHECK" "$tmp/pbstore" >/dev/null 2>&1; check [ "$?" != 0 ]
done

# 5. a program with another release date, or without a trusted key, is refused
{ printf 'FAKE\0PBSTORE_APP_VERSION=%s\0PBSTORE_RELEASE_DATE=2001-01-01\0' "$APPV"; keys_bytes; } > "$tmp/pbstore"
"$CHECK" "$tmp/pbstore" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
check grep -q "release date '2001-01-01'" "$tmp/err"
{ printf 'FAKE\0PBSTORE_APP_VERSION=%s\0PBSTORE_RELEASE_DATE=%s\0' "$APPV" "$DATE"; } > "$tmp/pbstore"
"$CHECK" "$tmp/pbstore" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
check grep -q "trusted key" "$tmp/err"
fake "$APPV"
"$CHECK" "$tmp/pbstore" >/dev/null 2>&1; check [ "$?" = 0 ]

# 6. a dev marker in a release package is refused; a dev package must carry it
fake "$APPV" "PBSTORE_DEV_BUILD_MARKER"
"$CHECK" "$tmp/pbstore" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
check grep -q "dev build, not a release" "$tmp/err"
"$CHECK" --dev "$tmp/pbstore" >/dev/null 2>&1; check [ "$?" = 0 ]
fake "$APPV"
"$CHECK" --dev "$tmp/pbstore" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
check grep -q "must carry PBSTORE_DEV_BUILD_MARKER" "$tmp/err"

# 7. dev and test packages: a version below the release, with or without the probe, goes to dist-dev/ and never to dist/
fake "$lower" "PBSTORE_DEV_BUILD_MARKER"
rm -rf "$tmp/work"; mkdir -p "$tmp/work"
( cd "$tmp/work" && "$SCRIPT" "$tmp/pbstore" "$lower" ) >/dev/null 2>"$tmp/err"; check [ "$?" = 0 ]
check test -f "$tmp/work/dist-dev/pbstore-inkpad-one-$lower.zip"
check_not test -e "$tmp/work/dist"
( cd "$tmp/work" && "$SCRIPT" "$tmp/pbstore" "$lower" dist ) >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]          # never to dist/
check grep -q "never to dist/" "$tmp/err"
check_not test -e "$tmp/work/dist"
printf 'FAKE-PROBE\0' > "$tmp/probe"
( cd "$tmp/work" && "$SCRIPT" "$tmp/pbstore" "$lower" dist-dev "$tmp/probe" ) >/dev/null 2>&1; check [ "$?" = 0 ]
check unzip -Z1 "$tmp/work/dist-dev/pbstore-inkpad-one-$lower.zip" | grep -qx 'applications/pbstore-probe.bin'
# a probe in a package of the release version is refused (a probe never ships in a release)
fake "$APPV"
"$SCRIPT" "$tmp/pbstore" "$APPV" "$tmp/out7" "$tmp/probe" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
check grep -q "never ships in a release" "$tmp/err"
# a dev package without the marker is refused
fake "$lower"
"$SCRIPT" "$tmp/pbstore" "$lower" "$tmp/work/dist-dev" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
check grep -q "must carry PBSTORE_DEV_BUILD_MARKER" "$tmp/err"

# 8. a version above the release is refused unless --allow-test-version is given
fake "$above" "PBSTORE_DEV_BUILD_MARKER"
"$SCRIPT" "$tmp/pbstore" "$above" "$tmp/work/dist-dev" >/dev/null 2>"$tmp/err"; check [ "$?" != 0 ]
check grep -q "allow-test-version" "$tmp/err"
"$SCRIPT" --allow-test-version "$tmp/pbstore" "$above" "$tmp/work/dist-dev" >/dev/null 2>&1; check [ "$?" = 0 ]
check test -f "$tmp/work/dist-dev/pbstore-inkpad-one-$above.zip"

# 9. the size budget
head -c 3000 /dev/zero > "$tmp/big.zip"
"$SCRIPT" --check-size "$tmp/big.zip" 2000 >/dev/null 2>&1; check [ "$?" != 0 ]
"$SCRIPT" --check-size "$tmp/big.zip" 3000 >/dev/null 2>&1; check [ "$?" = 0 ]

# 10. the script proves its own size check
"$SCRIPT" --self-test >/dev/null 2>&1; check [ "$?" = 0 ]

[ "$fail" = 0 ] && echo "package script tests passed"
exit "$fail"
